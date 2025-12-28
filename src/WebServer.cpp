// WebServer.cpp - ESP-IDF HTTP Server Implementation
#include "ESPWiFi.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
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
  // HTTP server runs in its own task; stack is a common “heap killer” if set
  // too big. 4096 is default in IDF; bump only if you see stack overflows
  // during handlers.
  config.stack_size = 8192;

  // Start the HTTP server
  esp_err_t ret = httpd_start(&webServer, &config);
  if (ret != ESP_OK) {
    log(ERROR, "❌ Failed to start HTTP server: %s", esp_err_to_name(ret));
    webServerStarted = false;
    return;
  }
  webServerStarted = true;

  // Restart endpoint
  httpd_uri_t restart_route = {.uri = "/api/restart",
                               .method = HTTP_POST,
                               .handler = [](httpd_req_t *req) -> esp_err_t {
                                 ESPWIFI_ROUTE_GUARD(req, espwifi, clientInfo);
                                 espwifi->sendJsonResponse(
                                     req, 200, "{\"status\":\"restarting\"}",
                                     &clientInfo);
                                 vTaskDelay(pdMS_TO_TICKS(500));
                                 esp_restart();
                                 return ESP_OK;
                               },
                               .user_ctx = this};
  httpd_register_uri_handler(webServer, &restart_route);

  // Global CORS preflight handler (covers all routes)
  // ESP-IDF requires an explicit handler for OPTIONS; otherwise preflights 404.
  httpd_uri_t options_route = {.uri = "/*",
                               .method = HTTP_OPTIONS,
                               .handler = [](httpd_req_t *req) -> esp_err_t {
                                 ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
                                 if (espwifi == nullptr) {
                                   httpd_resp_send_500(req);
                                   return ESP_OK;
                                 }
                                 espwifi->handleCorsPreflight(req);
                                 return ESP_OK;
                               },
                               .user_ctx = this};
  httpd_register_uri_handler(webServer, &options_route);

  // srvAll();

  webServerStarted = true;
  log(INFO, "🗄️ HTTP Web Server started");
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

esp_err_t ESPWiFi::verifyRequest(httpd_req_t *req, std::string *outClientInfo,
                                 bool requireAuth) {
  ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
  if (espwifi == nullptr) {
    httpd_resp_send_500(req);
    return ESP_ERR_INVALID_STATE;
  }

  // Handle OPTIONS requests automatically (CORS preflight)
  if (req->method == HTTP_OPTIONS) {
    espwifi->handleCorsPreflight(req);
    return ESP_ERR_HTTPD_RESP_SEND;
  }

  // Add CORS headers to all responses
  espwifi->addCORS(req);

  // Capture early; slow/streaming sends may lose socket/headers if client
  // resets.
  std::string clientInfo;
  if (outClientInfo != nullptr) {
    clientInfo = espwifi->getClientInfo(req);
  }

  if (!requireAuth) {
    if (outClientInfo != nullptr) {
      *outClientInfo = std::move(clientInfo);
    }
    return ESP_OK;
  }

  // Check if authorized
  if (!espwifi->authorized(req)) {
    if (clientInfo.empty()) {
      clientInfo = espwifi->getClientInfo(req);
    }
    espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}",
                              &clientInfo);
    return ESP_ERR_HTTPD_INVALID_REQ; // Don't continue with handler
  }

  if (outClientInfo != nullptr) {
    *outClientInfo = std::move(clientInfo);
  }
  return ESP_OK; // Verification passed, continue with handler
}

// Helper to get HTTP method as string for logging
const char *ESPWiFi::getMethodString(int method) {
  switch (static_cast<httpd_method_t>(method)) {
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

std::string ESPWiFi::getClientInfo(httpd_req_t *req) {
  // Get request information for logging
  const char *method = getMethodString(req->method);
  const char *uri = req->uri;
  const char *userAgent = "-";

  // Best-effort client IP extraction (capture early; socket may later die)
  char remote_ip[64];
  strncpy(remote_ip, "-", sizeof(remote_ip) - 1);
  remote_ip[sizeof(remote_ip) - 1] = '\0';

  int sockfd = httpd_req_to_sockfd(req);
  if (sockfd >= 0) {
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0) {
      if (addr.ss_family == AF_INET) {
        struct sockaddr_in *a = (struct sockaddr_in *)&addr;
        (void)inet_ntop(AF_INET, &a->sin_addr, remote_ip, sizeof(remote_ip));
      } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)&addr;
        // If this is an IPv4 client represented as an IPv4-mapped IPv6 address
        // (::ffff:a.b.c.d), log it as plain IPv4 for readability.
        const uint8_t *b = (const uint8_t *)&a6->sin6_addr;
        const bool isV4Mapped =
            (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 &&
             b[5] == 0 && b[6] == 0 && b[7] == 0 && b[8] == 0 && b[9] == 0 &&
             b[10] == 0xff && b[11] == 0xff);
        if (isV4Mapped) {
          struct in_addr v4;
          memcpy(&v4, b + 12, sizeof(v4));
          (void)inet_ntop(AF_INET, &v4, remote_ip, sizeof(remote_ip));
        } else {
          (void)inet_ntop(AF_INET6, &a6->sin6_addr, remote_ip,
                          sizeof(remote_ip));
        }
      }
    }
  }

  // Get User-Agent header if available (bounded; avoid heap allocs)
  char userAgentBuf[128];
  size_t user_agent_len = httpd_req_get_hdr_value_len(req, "User-Agent");
  if (user_agent_len > 0) {
    userAgentBuf[0] = '\0';
    if (httpd_req_get_hdr_value_str(req, "User-Agent", userAgentBuf,
                                    sizeof(userAgentBuf)) == ESP_OK &&
        userAgentBuf[0] != '\0') {
      userAgent = userAgentBuf;
    }
  }

  char clientInfoBuf[1024];
  (void)snprintf(clientInfoBuf, sizeof(clientInfoBuf), "%s - %s - %s - %s",
                 remote_ip, method, uri, userAgent);
  return std::string(clientInfoBuf);
}

void ESPWiFi::logAccess(int statusCode, const std::string &clientInfo,
                        size_t bytesSent) {

  std::string status = getStatusFromCode(statusCode);
  log(ACCESS, "%s - %s - %zu", status.c_str(), clientInfo.c_str(), bytesSent);
}

void ESPWiFi::sendJsonResponse(httpd_req_t *req, int statusCode,
                               const std::string &jsonBody,
                               const std::string *clientInfo) {
  addCORS(req);
  httpd_resp_set_type(req, "application/json");

  // Reuse captured clientInfo when available; otherwise capture now.
  std::string clientInfoLocal;
  const std::string &clientInfoRef =
      (clientInfo != nullptr) ? *clientInfo
                              : (clientInfoLocal = getClientInfo(req));

  std::string httpStatus = getStatusFromCode(statusCode);
  httpd_resp_set_status(req, httpStatus.c_str());

  // For larger JSON payloads, stream in chunks with tiny yields to avoid
  // starving the httpd task / triggering the task watchdog on slow links.
  constexpr size_t CHUNK_SIZE = 1024;
  esp_err_t ret = ESP_OK;
  size_t sent = 0;

  if (jsonBody.size() <= CHUNK_SIZE) {
    ret = httpd_resp_send(req, jsonBody.c_str(), HTTPD_RESP_USE_STRLEN);
    sent = (ret == ESP_OK) ? jsonBody.size() : 0;
  } else {
    const char *data = jsonBody.data();
    size_t remaining = jsonBody.size();
    while (remaining > 0) {
      size_t toSend = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
      ret = httpd_resp_send_chunk(req, data + sent, toSend);
      if (ret != ESP_OK) {
        break;
      }
      sent += toSend;
      remaining -= toSend;
      yield();
    }
    // Finalize chunked transfer (even on error; best-effort)
    (void)httpd_resp_send_chunk(req, nullptr, 0);
  }

  // Single access log per request
  logAccess(statusCode, clientInfoRef, sent);
}

esp_err_t ESPWiFi::sendFileResponse(httpd_req_t *req,
                                    const std::string &filePath,
                                    const std::string *clientInfo) {
  addCORS(req);

  // Reuse captured clientInfo when available; otherwise capture now.
  std::string clientInfoLocal;
  const std::string &clientInfoRef =
      (clientInfo != nullptr) ? *clientInfo
                              : (clientInfoLocal = getClientInfo(req));

  // Check if LFS is initialized
  if (!littleFsInitialized) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    const char *body = "Filesystem not available";
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }

  // Construct full path
  std::string fullPath = lfsMountPoint + filePath;

  // Check if file exists first
  struct stat fileStat;
  int statResult = stat(fullPath.c_str(), &fileStat);
  if (statResult != 0) {
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
    yield(); // Yield before each chunk
    size_t toRead = (fileSize - totalSent < CHUNK_SIZE) ? (fileSize - totalSent)
                                                        : CHUNK_SIZE;

    size_t bytesRead = fread(buffer, 1, toRead, file);
    if (bytesRead == 0) {
      printf("💔  fread returned 0, expected %zu bytes\n", toRead);
      ret = ESP_FAIL;
      break;
    }
    yield(); // Yield after file I/O

    // Send chunk
    ret = httpd_resp_send_chunk(req, buffer, bytesRead);
    if (ret != ESP_OK) {
      printf("💔  httpd_resp_send_chunk failed at %zu bytes, error: %s\n",
             totalSent, esp_err_to_name(ret));
      break;
    }
    yield(); // Yield after network I/O

    totalSent += bytesRead;
  }

  // Finalize chunked transfer
  if (ret == ESP_OK) {
    yield();                                      // Yield before finalizing
    ret = httpd_resp_send_chunk(req, nullptr, 0); // End chunked transfer
    if (ret != ESP_OK) {
      printf("💔  Failed to finalize chunked transfer: %s\n",
             esp_err_to_name(ret));
    }
    yield(); // Yield after finalizing
  }

  free(buffer);
  fclose(file);

  if (ret != ESP_OK || totalSent != (size_t)fileSize) {
    printf("💔  File send incomplete: sent %zu of %ld bytes\n", totalSent,
           fileSize);
    return ESP_FAIL;
  }

  // Default response is 200 OK for file responses
  logAccess(200, clientInfoRef, totalSent);
  return ESP_OK;
}

void ESPWiFi::srvInfo() {
  if (!webServer) {
    log(ERROR, "Cannot start info API /api/info: web server not initialized");
    return;
  }

  // Info endpoint
  httpd_uri_t info_route = {
      .uri = "/info",
      .method = HTTP_GET,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWIFI_ROUTE_GUARD(req, espwifi, clientInfo);
        {
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
          std::string deviceName =
              espwifi->config["deviceName"].as<std::string>();
          jsonDoc["mdns"] = deviceName + ".local";

          // Yield to prevent watchdog timeout
          vTaskDelay(pdMS_TO_TICKS(10));

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

          // Yield to prevent watchdog timeout
          vTaskDelay(pdMS_TO_TICKS(10));

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

          // Yield to prevent watchdog timeout
          vTaskDelay(pdMS_TO_TICKS(10));

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

          // Yield to prevent watchdog timeout
          vTaskDelay(pdMS_TO_TICKS(10));

          // SD card storage information if available
          if (espwifi->sdCardInitialized) {
            size_t sdTotalBytes, sdUsedBytes, sdFreeBytes;
            espwifi->getStorageInfo("sd", sdTotalBytes, sdUsedBytes,
                                    sdFreeBytes);
            jsonDoc["sd_free"] = sdFreeBytes;
            jsonDoc["sd_used"] = sdUsedBytes;
            jsonDoc["sd_total"] = sdTotalBytes;
          }

          // Serialize JSON to string
          std::string jsonResponse;
          serializeJson(jsonDoc, jsonResponse);

          espwifi->sendJsonResponse(req, 200, jsonResponse, &clientInfo);
          return ESP_OK;
        }
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &info_route);
}

void ESPWiFi::srvRoot() {
  if (!webServer) {
    log(ERROR, "Cannot start root route /: web server not initialized");
    return;
  }

  // Root route - serve index.html from LFS (no auth required)
  httpd_uri_t root_route = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWIFI_ROUTE_GUARD_NOAUTH(req, espwifi, clientInfo);
        esp_err_t ret =
            espwifi->sendFileResponse(req, "/index.html", &clientInfo);
        if (ret != ESP_OK) {
          // sendFileResponse failed, send 404 response
          espwifi->sendJsonResponse(req, 404, "{\"error\":\"Not found\"}",
                                    &clientInfo);
          return ESP_OK; // Response sent, return OK
        }
        return ESP_OK;
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &root_route);
}

void ESPWiFi::srvWildcard() {
  if (!webServer) {
    log(ERROR, "Cannot start wildcard route /* : web server not initialized");
    return;
  }

  // GET handler for static files
  httpd_uri_t wildcard_route = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWIFI_ROUTE_GUARD(req, espwifi, clientInfo);

        esp_err_t ret = espwifi->sendFileResponse(req, req->uri, &clientInfo);
        if (ret != ESP_OK) {
          espwifi->sendJsonResponse(req, 404, "{\"error\":\"Not found\"}");
          return ESP_OK; // Return OK since we sent a response
        }

        return ESP_OK;
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &wildcard_route);
}