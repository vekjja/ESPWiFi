// WebServer.cpp - ESP-IDF HTTP Server Implementation
#include "ESPWiFi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <cstring>
#include <string>

void ESPWiFi::startWebServer() {
  if (webServerStarted) {
    return;
  }

  // Configure HTTP server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_len = 512;
  config.max_open_sockets = 7;
  config.max_uri_handlers = 16; // Increase to accommodate all routes + wildcard
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard; // Enable wildcard matching

  // Start the HTTP server
  esp_err_t ret = httpd_start(&webServer, &config);
  if (ret != ESP_OK) {
    log(ERROR, "❌ Failed to start HTTP server: %s", esp_err_to_name(ret));
    webServerStarted = false;
    return;
  }
  webServerStarted = true;

  // Root route - serve index.html from LFS (no auth required)
  HTTPRoute("/", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    return espwifi->sendFileResponse(req, "/index.html");
  });

  // Restart endpoint
  HTTPRoute("/api/restart", HTTP_POST, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
      espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
      return ESP_OK;
    }

    espwifi->sendJsonResponse(req, 200, "{\"status\":\"restarting\"}");

    // Restart after a short delay
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
  });

  srvAll();

  webServerStarted = true;
  log(INFO, "🗄️  HTTP Web Server started");
  log(DEBUG, "\thttp://%s:%d", getHostname().c_str(), 80);
  log(DEBUG, "\thttp://%s:%d", ipAddress().c_str(), 80);
}

void ESPWiFi::srvAll() {
  srvFS();
  srvLog();
  srvInfo();
  srvConfig();
  srvAuth();
  srvGPIO();
  srvWildcard();
}

void ESPWiFi::addCORS(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods",
                     "GET, POST, PUT, DELETE, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers",
                     "Content-Type, Authorization");
}

void ESPWiFi::handleCorsPreflight(httpd_req_t *req) {
  addCORS(req);
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, nullptr, 0);
}

void ESPWiFi::sendJsonResponse(httpd_req_t *req, int statusCode,
                               const std::string &jsonBody) {
  addCORS(req);
  httpd_resp_set_type(req, "application/json");

  // Format status code properly (e.g., "200 OK", "404 Not Found")
  char status_str[32];
  const char *status_text = "OK";
  if (statusCode == 201)
    status_text = "Created";
  else if (statusCode == 204)
    status_text = "No Content";
  else if (statusCode == 400)
    status_text = "Bad Request";
  else if (statusCode == 401)
    status_text = "Unauthorized";
  else if (statusCode == 404)
    status_text = "Not Found";
  else if (statusCode == 500)
    status_text = "Internal Server Error";
  snprintf(status_str, sizeof(status_str), "%d %s", statusCode, status_text);
  httpd_resp_set_status(req, status_str);

  httpd_resp_send(req, jsonBody.c_str(), HTTPD_RESP_USE_STRLEN);
}

esp_err_t ESPWiFi::sendFileResponse(httpd_req_t *req,
                                    const std::string &filePath) {
  addCORS(req);

  // Check if LFS is initialized
  if (!littleFsInitialized) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Filesystem not available", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }

  // Construct full path
  std::string fullPath = lfsMountPoint + filePath;

  // Check if file exists first (like Arduino's filesystem->exists())
  struct stat fileStat;
  if (stat(fullPath.c_str(), &fileStat) != 0) {
    // File doesn't exist - don't send response here, let caller handle it
    return ESP_FAIL;
  }

  // Check if it's actually a file (not a directory)
  if (S_ISDIR(fileStat.st_mode)) {
    // Path is a directory, not a file
    return ESP_FAIL;
  }

  // Open file from LFS
  FILE *file = fopen(fullPath.c_str(), "rb");
  if (!file) {
    return ESP_FAIL;
  }

  // Get file size
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return ESP_FAIL;
  }

  long fileSize = ftell(file);
  if (fileSize < 0) {
    fclose(file);
    return ESP_FAIL;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return ESP_FAIL;
  }

  if (fileSize == 0) {
    fclose(file);
    return ESP_FAIL;
  }

  // Allocate buffer for file content
  char *buffer = (char *)malloc(fileSize);
  if (!buffer) {
    fclose(file);
    return ESP_FAIL;
  }

  // Read file content
  size_t bytesRead = fread(buffer, 1, fileSize, file);
  fclose(file);

  if (bytesRead != (size_t)fileSize) {
    free(buffer);
    return ESP_FAIL;
  }

  // Determine content type based on file extension
  std::string contentType = getContentType(filePath);
  httpd_resp_set_type(req, contentType.c_str());

  // Send response
  esp_err_t ret = httpd_resp_send(req, buffer, fileSize);
  free(buffer);

  return ret;
}

void ESPWiFi::srvInfo() {
  if (!webServerStarted) {
    log(ERROR, "Cannot start info API /api/info: web server not initialized");
    return;
  }

  HTTPRoute("/api/info", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    espwifi->sendJsonResponse(req, 200, "{\"info\":\"ok\"}");
    return ESP_OK;
  });
}

void ESPWiFi::srvWildcard() {
  if (!webServerStarted) {
    log(ERROR, "Cannot start wildcard route /* : web server not initialized");
    return;
  }

  HTTPRoute("/*", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    esp_err_t ret = espwifi->sendFileResponse(req, req->uri);
    if (ret != ESP_OK) {
      espwifi->sendJsonResponse(req, 404, "{\"error\":\"Not found\"}");
      return ESP_OK; // Return OK since we sent a response
    }
    return ESP_OK;
  });
}