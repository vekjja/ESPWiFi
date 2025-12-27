// WebServer.cpp - ESP-IDF HTTP Server Implementation
#include "ESPWiFi.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
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
  config.max_uri_handlers = 32;
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
  HTTPRoute("/api/restart", HTTP_POST, [](httpd_req_t *req) -> esp_err_t {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi->verify(req, true) != ESP_OK) {
      return ESP_OK; // Response already sent (OPTIONS or error)
    }

    // Handler:
    {
      ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
      espwifi->sendJsonResponse(req, 200, "{\"status\":\"restarting\"}");

      // Restart after a short delay
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_restart();

      return ESP_OK;
    }
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

esp_err_t ESPWiFi::verify(httpd_req_t *req, bool requireAuth) {
  ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
  if (espwifi == nullptr) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  // Handle OPTIONS requests automatically (CORS preflight)
  if (req->method == HTTP_OPTIONS) {
    espwifi->handleCorsPreflight(req);
    return ESP_FAIL; // Don't continue with handler
  }

  // Add CORS headers to all responses
  espwifi->addCORS(req);

  // Check authentication if required
  if (requireAuth && !espwifi->authorized(req)) {
    espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
    return ESP_FAIL; // Don't continue with handler
  }

  return ESP_OK; // Verification passed, continue with handler
}

// Helper to get HTTP method as string for logging
const char *ESPWiFi::getMethodString(httpd_method_t method) {
  switch (method) {
  case HTTP_GET:
    return "GET";
  case HTTP_POST:
    return "POST";
  case HTTP_PUT:
    return "PUT";
  case HTTP_DELETE:
    return "DELETE";
  case HTTP_PATCH:
    return "PATCH";
  case HTTP_HEAD:
    return "HEAD";
  case HTTP_OPTIONS:
    return "OPTIONS";
  default:
    return "UNKNOWN";
  }
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
  if (S_ISDIR(fileStat.st_mode)) {
    // Path is a directory, not a file
    return ESP_FAIL;
  }

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

  // Use chunked encoding for all files to allow yields between chunks
  // This prevents watchdog timeouts on slow networks
  const size_t CHUNK_SIZE =
      8192; // 8KB chunks (smaller chunks = more frequent yields)

  esp_err_t ret = ESP_OK;

  // Enable buffered I/O for better performance
  setvbuf(file, nullptr, _IOFBF, CHUNK_SIZE);

  char *buffer = (char *)malloc(CHUNK_SIZE);
  if (!buffer) {
    printf("💔 malloc failed for chunk buffer of %zu bytes\n", CHUNK_SIZE);
    fclose(file);
    return ESP_FAIL;
  }

  size_t totalSent = 0;

  // Stream file in chunks with frequent yields to prevent watchdog timeout
  while (totalSent < (size_t)fileSize && ret == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(1)); // Yield before each chunk
    size_t toRead = (fileSize - totalSent < CHUNK_SIZE) ? (fileSize - totalSent)
                                                        : CHUNK_SIZE;

    size_t bytesRead = fread(buffer, 1, toRead, file);
    if (bytesRead == 0) {
      printf("💔  fread returned 0, expected %zu bytes\n", toRead);
      ret = ESP_FAIL;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Yield after file I/O

    // Send chunk
    ret = httpd_resp_send_chunk(req, buffer, bytesRead);
    if (ret != ESP_OK) {
      printf("💔  httpd_resp_send_chunk failed at %zu bytes, error: %s\n",
             totalSent, esp_err_to_name(ret));
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O

    totalSent += bytesRead;
  }

  // Finalize chunked transfer
  if (ret == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(1));                 // Yield before finalizing
    ret = httpd_resp_send_chunk(req, nullptr, 0); // End chunked transfer
    if (ret != ESP_OK) {
      printf("💔  Failed to finalize chunked transfer: %s\n",
             esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Yield after finalizing
  }

  free(buffer);
  fclose(file);

  if (ret != ESP_OK || totalSent != (size_t)fileSize) {
    printf("💔  File send incomplete: sent %zu of %ld bytes\n", totalSent,
           fileSize);
    return ESP_FAIL;
  }

  return ESP_OK;
}

void ESPWiFi::srvInfo() {
  if (!webServer) {
    log(ERROR, "Cannot start info API /api/info: web server not initialized");
    return;
  }

  // Info endpoint
  HTTPRoute("/info", HTTP_GET, [](httpd_req_t *req) -> esp_err_t {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi->verify(req, true) != ESP_OK) {
      return ESP_OK; // Response already sent (OPTIONS or error)
    }

    // Handler:
    {
      ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

      JsonDocument jsonDoc;

      // Uptime in seconds
      jsonDoc["uptime"] = millis() / 1000;

      // IP address - get from current network interface
      std::string ip = espwifi->ipAddress();
      jsonDoc["ip"] = ip;

      // MAC address - try WiFi interface first, fallback to reading MAC
      // directly
      uint8_t mac[6];
      esp_err_t mac_ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
      if (mac_ret != ESP_OK) {
        // Fallback: read MAC directly from hardware
        mac_ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
      }
      if (mac_ret == ESP_OK) {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        jsonDoc["mac"] = std::string(macStr);
      } else {
        jsonDoc["mac"] = "";
      }

      // Hostname
      std::string hostname = espwifi->getHostname();
      jsonDoc["hostname"] = hostname;

      // AP SSID - construct from config the same way as when starting AP
      std::string ap_ssid =
          espwifi->config["wifi"]["ap"]["ssid"].as<std::string>();
      jsonDoc["ap_ssid"] = ap_ssid + "-" + hostname;

      // mDNS
      std::string deviceName = espwifi->config["deviceName"].as<std::string>();
      jsonDoc["mdns"] = deviceName + ".local";

      // Chip model and SDK version
      esp_chip_info_t chip_info;
      esp_chip_info(&chip_info);
      char chip_model[32];
      // Format chip model based on chip_info.model
      if (chip_info.model == CHIP_ESP32C3) {
        snprintf(chip_model, sizeof(chip_model), "ESP32-C3");
      } else if (chip_info.model == CHIP_ESP32) {
        snprintf(chip_model, sizeof(chip_model), "ESP32");
      } else if (chip_info.model == CHIP_ESP32S2) {
        snprintf(chip_model, sizeof(chip_model), "ESP32-S2");
      } else if (chip_info.model == CHIP_ESP32S3) {
        snprintf(chip_model, sizeof(chip_model), "ESP32-S3");
      } else {
        snprintf(chip_model, sizeof(chip_model), "ESP32-Unknown");
      }
      jsonDoc["chip"] = std::string(chip_model);
      jsonDoc["sdk_version"] = std::string(esp_get_idf_version());

      // Heap information
      size_t free_heap = esp_get_free_heap_size();
      size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
      jsonDoc["free_heap"] = free_heap;
      jsonDoc["total_heap"] = total_heap;
      jsonDoc["used_heap"] = total_heap - free_heap;

      // WiFi connection status and info
      wifi_ap_record_t ap_info;
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        // WiFi is connected as station
        char ssid_str[33];
        memcpy(ssid_str, ap_info.ssid, 32);
        ssid_str[32] = '\0';
        jsonDoc["client_ssid"] = std::string(ssid_str);
        jsonDoc["rssi"] = ap_info.rssi;
      }

      // LittleFS storage information
      if (espwifi->littleFsInitialized) {
        size_t totalBytes, usedBytes, freeBytes;
        espwifi->getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
        jsonDoc["littlefs_free"] = freeBytes;
        jsonDoc["littlefs_used"] = usedBytes;
        jsonDoc["littlefs_total"] = totalBytes;
      } else {
        jsonDoc["littlefs_free"] = 0;
        jsonDoc["littlefs_used"] = 0;
        jsonDoc["littlefs_total"] = 0;
      }

      // SD card storage information if available
      if (espwifi->sdCardInitialized) {
        size_t sdTotalBytes, sdUsedBytes, sdFreeBytes;
        espwifi->getStorageInfo("sd", sdTotalBytes, sdUsedBytes, sdFreeBytes);
        jsonDoc["sd_free"] = sdFreeBytes;
        jsonDoc["sd_used"] = sdUsedBytes;
        jsonDoc["sd_total"] = sdTotalBytes;
      }

      // Serialize JSON to string
      std::string jsonResponse;
      serializeJson(jsonDoc, jsonResponse);

      espwifi->sendJsonResponse(req, 200, jsonResponse);
      return ESP_OK;
    }
  });
}

void ESPWiFi::srvWildcard() {
  if (!webServer) {
    log(ERROR, "Cannot start wildcard route /* : web server not initialized");
    return;
  }

  HTTPRoute("/*", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    std::string uri(req->uri);
    if (uri.find("/api/") == 0) {
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
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