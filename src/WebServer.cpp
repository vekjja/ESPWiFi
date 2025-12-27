// WebServer.cpp - ESP-IDF HTTP Server Implementation
#include "ESPWiFi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <cstring>
#include <string>

void ESPWiFi::startWebServer() {
  if (webServerStarted) {
    log(WARNING, "⚠️ Web server already started");
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

  // Root route - serve index.html from LFS (no auth required)
  HTTPRoute("/", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    return espwifi->sendFileResponse(req, "/index.html");
  });

  // Info endpoint
  HTTPRoute("/api/info", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
      espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
      return ESP_OK;
    }

    // TODO: Generate espwifi info JSON
    std::string json =
        "{\"version\":\"" + espwifi->version() + "\",\"status\":\"ok\"}";
    espwifi->sendJsonResponse(req, 200, json);
    return ESP_OK;
  });

  // Config GET endpoint
  HTTPRoute("/api/config", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
      espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
      return ESP_OK;
    }

    // Serialize config to JSON
    std::string json;
    serializeJson(espwifi->config, json);
    espwifi->sendJsonResponse(req, 200, json);
    return ESP_OK;
  });

  // Config POST endpoint
  HTTPRoute("/api/config", HTTP_POST, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
      espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
      return ESP_OK;
    }

    // Read request body
    size_t content_len = req->content_len;
    if (content_len > 4096) { // Limit to 4KB
      httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE,
                          "Request body too large");
      return ESP_FAIL;
    }

    char *content = (char *)malloc(content_len + 1);
    if (content == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
      free(content);
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_408(req);
      }
      return ESP_FAIL;
    }

    content[content_len] = '\0';
    std::string json_body(content);
    free(content);

    // TODO: Parse and update config
    espwifi->sendJsonResponse(req, 200, "{\"status\":\"ok\"}");
    return ESP_OK;
  });

  // Files endpoint
  HTTPRoute("/api/files", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
      espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
      return ESP_OK;
    }

    // TODO: List files and return JSON array
    espwifi->sendJsonResponse(req, 200, "{\"files\":[]}");
    return ESP_OK;
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

  // Auth endpoint (no auth required for login)
  HTTPRoute("/api/auth", HTTP_POST, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    // Read request body
    size_t content_len = req->content_len;
    if (content_len > 512) {
      httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE,
                          "Request body too large");
      return ESP_FAIL;
    }

    char *content = (char *)malloc(content_len + 1);
    if (content == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
      free(content);
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_408(req);
      }
      return ESP_FAIL;
    }

    content[content_len] = '\0';

    // TODO: Parse credentials and validate, generate token
    std::string token = espwifi->generateToken();
    std::string json = "{\"token\":\"" + token + "\"}";

    free(content);
    espwifi->sendJsonResponse(req, 200, json);
    return ESP_OK;
  });

  // Log endpoint
  HTTPRoute("/api/log", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
      espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
      return ESP_OK;
    }

    // TODO: Read log file and return content
    espwifi->sendJsonResponse(req, 200, "{\"log\":\"\"}");
    return ESP_OK;
  });

  HTTPRoute("/*", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    esp_err_t ret = espwifi->sendFileResponse(req, req->uri);
    if (ret != ESP_OK) {
      // File not found or error - send 404 response
      // Note: sendFileResponse may have already sent a response (e.g., 503),
      // but if it just returned ESP_FAIL, we need to send 404 here
      espwifi->sendJsonResponse(req, 404, "{\"error\":\"Not found\"}");
      return ESP_OK; // Return OK since we sent a response
    }
    return ESP_OK;
  });

  webServerStarted = true;
  log(INFO, "🗄️  HTTP Web Server started");
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

// void ESPWiFi::HTTPRoute(httpd_uri_t route) {
//   if (!webServer) {
//     log(ERROR, "❌ Cannot register route: web server not initialized");
//     return;
//   }
//   route.user_ctx = this; // Ensure user_ctx is set to this instance
//   httpd_register_uri_handler(webServer, &route);
// }

bool ESPWiFi::authorized(httpd_req_t *req) {
  if (!authEnabled()) {
    return true;
  }

  // Get Authorization header
  size_t auth_hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (auth_hdr_len == 0) {
    return false;
  }

  char *auth_hdr = (char *)malloc(auth_hdr_len + 1);
  if (auth_hdr == nullptr) {
    return false;
  }

  httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, auth_hdr_len + 1);

  // Check for Bearer token
  std::string auth_str(auth_hdr);
  free(auth_hdr);

  if (auth_str.find("Bearer ") == 0) {
    std::string token = auth_str.substr(7);
    // TODO: Validate token against stored tokens
    // For now, simple check - you should implement proper token validation
    return !token.empty();
  }

  return false;
}
