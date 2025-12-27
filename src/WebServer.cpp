// WebServer.cpp - ESP-IDF HTTP Server Implementation
#include "ESPWiFi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <cstring>
#include <errno.h>
#include <string>
#include <sys/stat.h>

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

  printf("Sending file: %s\n", fullPath.c_str());

  // Check if file exists first
  struct stat fileStat;
  int statResult = stat(fullPath.c_str(), &fileStat);
  if (statResult != 0) {
    printf("stat failed for %s, errno: %d\n", fullPath.c_str(), errno);
    return ESP_FAIL;
  }

  printf("File size: %ld bytes, is_dir: %d\n", fileStat.st_size,
         S_ISDIR(fileStat.st_mode));

  // Check if it's actually a file (not a directory)
  // if (S_ISDIR(fileStat.st_mode)) {
  //   // Path is a directory, not a file
  //   return ESP_FAIL;
  // }

  // Open file from LFS
  FILE *file = fopen(fullPath.c_str(), "rb");
  if (!file) {
    printf("fopen failed for %s, errno: %d\n", fullPath.c_str(), errno);
    return ESP_FAIL;
  }

  // Get file size
  if (fseek(file, 0, SEEK_END) != 0) {
    printf("fseek SEEK_END failed, errno: %d\n", errno);
    fclose(file);
    return ESP_FAIL;
  }

  long fileSize = ftell(file);
  if (fileSize < 0) {
    printf("ftell failed, errno: %d\n", errno);
    fclose(file);
    return ESP_FAIL;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    printf("fseek SEEK_SET failed, errno: %d\n", errno);
    fclose(file);
    return ESP_FAIL;
  }

  if (fileSize == 0) {
    printf("File size is 0\n");
    fclose(file);
    return ESP_FAIL;
  }

  // Determine content type based on file extension
  std::string contentType = getContentType(filePath);
  httpd_resp_set_type(req, contentType.c_str());

  // For small files (< 32KB), load entirely and send directly (faster, no
  // chunked encoding overhead) For large files, stream in chunks to avoid
  // memory issues
  const size_t SMALL_FILE_THRESHOLD = 32768;
  const size_t CHUNK_SIZE = 32768; // 32KB chunks

  esp_err_t ret = ESP_OK;

  if (fileSize <= SMALL_FILE_THRESHOLD) {
    // Small file - load entirely and send directly
    char *buffer = (char *)malloc(fileSize);
    if (!buffer) {
      printf("💔 malloc failed for small file buffer of %ld bytes\n", fileSize);
      fclose(file);
      return ESP_FAIL;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    fclose(file);

    if (bytesRead != (size_t)fileSize) {
      printf("fread incomplete: %zu of %ld bytes\n", bytesRead, fileSize);
      free(buffer);
      return ESP_FAIL;
    }

    ret = httpd_resp_send(req, buffer, fileSize);
    free(buffer);
    return ret;
  }

  // Large file - stream in chunks using chunked transfer encoding
  // Enable buffered I/O for better performance
  setvbuf(file, nullptr, _IOFBF, CHUNK_SIZE);

  char *buffer = (char *)malloc(CHUNK_SIZE);
  if (!buffer) {
    printf("💔 malloc failed for chunk buffer of %zu bytes\n", CHUNK_SIZE);
    fclose(file);
    return ESP_FAIL;
  }

  size_t totalSent = 0;

  // Stream file in chunks
  while (totalSent < (size_t)fileSize && ret == ESP_OK) {
    size_t toRead = (fileSize - totalSent < CHUNK_SIZE) ? (fileSize - totalSent)
                                                        : CHUNK_SIZE;

    size_t bytesRead = fread(buffer, 1, toRead, file);
    if (bytesRead == 0) {
      printf("fread returned 0, expected %zu bytes\n", toRead);
      ret = ESP_FAIL;
      break;
    }

    // Send chunk
    ret = httpd_resp_send_chunk(req, buffer, bytesRead);
    if (ret != ESP_OK) {
      printf("httpd_resp_send_chunk failed at %zu bytes, error: %s\n",
             totalSent, esp_err_to_name(ret));
      break;
    }

    totalSent += bytesRead;
  }

  // Finalize chunked transfer
  if (ret == ESP_OK) {
    ret = httpd_resp_send_chunk(req, nullptr, 0); // End chunked transfer
    if (ret != ESP_OK) {
      printf("Failed to finalize chunked transfer: %s\n", esp_err_to_name(ret));
    }
  }

  free(buffer);
  fclose(file);

  if (ret != ESP_OK || totalSent != (size_t)fileSize) {
    printf("File send incomplete: sent %zu of %ld bytes\n", totalSent,
           fileSize);
    return ESP_FAIL;
  }

  return ESP_OK;
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