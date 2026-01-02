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

// ESP-IDF includes the full request URI (including "?query") in req->uri.
// Route matching must ignore the query string, otherwise "/api/foo" will not
// match "/api/foo?x=1" and requests can fall through to the catch-all handler.
static bool uri_match_no_query(const char *uri, const char *uri_template,
                               size_t tpl_len) {
  if (uri == nullptr || uri_template == nullptr) {
    return false;
  }

  // max_uri_len is configured below to 512; keep stack bounded.
  char pathOnly[512];
  size_t i = 0;
  for (; i < sizeof(pathOnly) - 1 && uri[i] != '\0' && uri[i] != '?'; ++i) {
    pathOnly[i] = uri[i];
  }
  pathOnly[i] = '\0';

  return httpd_uri_match_wildcard(pathOnly, uri_template, tpl_len);
}

static esp_err_t noopRouteHandler(ESPWiFi *espwifi, httpd_req_t *req,
                                  const std::string &clientInfo) {
  (void)espwifi;
  (void)req;
  (void)clientInfo;
  return ESP_OK;
}

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
  config.uri_match_fn = &uri_match_no_query; // wildcard matching (path-only)
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

  // Global CORS preflight handler (covers all routes)
  // ESP-IDF requires an explicit handler for OPTIONS; otherwise preflights 404.
  (void)registerRoute("/*", HTTP_OPTIONS, &noopRouteHandler);

  // Restart endpoint
  (void)registerRoute("/api/restart", HTTP_POST,
                      [](ESPWiFi *espwifi, httpd_req_t *req,
                         const std::string &clientInfo) -> esp_err_t {
                        (void)espwifi->sendJsonResponse(
                            req, 200, "{\"status\":\"restarting\"}",
                            &clientInfo);
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                        return ESP_OK;
                      });

  // srvAll();

  webServerStarted = true;
  log(INFO, "🗄️ HTTP Web Server started");
  log(DEBUG, "🗄️\thttp://%s:%d", getHostname().c_str(), 80);
  log(DEBUG, "🗄️\thttp://%s:%d", ipAddress().c_str(), 80);
}

esp_err_t ESPWiFi::routeTrampoline(httpd_req_t *req) {
  RouteCtx *ctx = (RouteCtx *)req->user_ctx;
  if (ctx == nullptr || ctx->self == nullptr || ctx->handler == nullptr) {
    httpd_resp_send_500(req);
    return ESP_OK;
  }

  std::string clientInfo;
  if (ctx->self->verifyRequest(req, &clientInfo) != ESP_OK) {
    return ESP_OK; // preflight or error already handled
  }

  return ctx->handler(ctx->self, req, clientInfo);
}

esp_err_t ESPWiFi::registerRoute(const char *uri, httpd_method_t method,
                                 RouteHandler handler) {
  if (!webServer) {
    log(ERROR, "Cannot register route %s: web server not initialized",
        (uri != nullptr) ? uri : "(null)");
    return ESP_ERR_INVALID_STATE;
  }
  if (uri == nullptr || handler == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  // Allocate a tiny per-route context. This runs at init time; keep it simple.
  RouteCtx *ctx = new (std::nothrow) RouteCtx();
  if (ctx == nullptr) {
    log(ERROR, "Cannot register route %s: out of memory", uri);
    return ESP_ERR_NO_MEM;
  }
  ctx->self = this;
  ctx->handler = handler;

  httpd_uri_t route = {
      .uri = uri,
      .method = method,
      .handler = &ESPWiFi::routeTrampoline,
      .user_ctx = ctx,
      // ESP-IDF adds fields over time;
      // explicitly initialize the newer ones.
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = nullptr,
  };

  esp_err_t ret = httpd_register_uri_handler(webServer, &route);
  if (ret != ESP_OK) {
    log(ERROR, "Failed to register route %s: %s", uri, esp_err_to_name(ret));
    delete ctx;
    return ret;
  }

  _routeContexts.push_back(ctx);
  return ESP_OK;
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

esp_err_t ESPWiFi::sendJsonResponse(httpd_req_t *req, int statusCode,
                                    const std::string &jsonBody,
                                    const std::string *clientInfo) {
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
      feedWatchDog();
    }
    // Finalize chunked transfer (even on error; best-effort)
    esp_err_t end_ret = httpd_resp_send_chunk(req, nullptr, 0);
    if (ret == ESP_OK && end_ret != ESP_OK) {
      ret = end_ret;
    }
  }

  // Single access log per request
  logAccess(statusCode, clientInfoRef, sent);
  return ret;
}

esp_err_t ESPWiFi::sendFileResponse(httpd_req_t *req,
                                    const std::string &filePath,
                                    const std::string *clientInfo) {

  // Reuse captured clientInfo when available; otherwise capture now.
  std::string clientInfoLocal;
  const std::string &clientInfoRef =
      (clientInfo != nullptr) ? *clientInfo
                              : (clientInfoLocal = getClientInfo(req));

  // Resolve which filesystem this path maps to.
  // - "/sd/..." is served from the SD mount (FATFS)
  // - "/lfs/..." is served from the LittleFS mount
  // - everything else is served from LittleFS (prefixed by lfsMountPoint)
  std::string fullPath;
  bool fsAvailable = false;
  if (filePath.rfind("/sd/", 0) == 0 || filePath == "/sd") {
    fsAvailable = (sdCard != nullptr);
    fullPath = filePath; // already includes mountpoint
  } else if (filePath.rfind("/lfs/", 0) == 0 || filePath == "/lfs") {
    fsAvailable = (lfs != nullptr);
    fullPath = filePath; // already includes mountpoint
  } else {
    fsAvailable = (lfs != nullptr);
    fullPath = lfsMountPoint + filePath;
  }

  if (!fsAvailable) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    const char *body = "Filesystem not available";
    esp_err_t ret = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    if (ret == ESP_OK) {
      logAccess(503, clientInfoRef, strlen(body));
      return ESP_OK;
    }
    logAccess(503, clientInfoRef, 0);
    return ret;
  }

  // Check if file exists first
  struct stat fileStat;
  int statResult = stat(fullPath.c_str(), &fileStat);
  if (statResult != 0) {
    return sendJsonResponse(req, 404, "{\"error\":\"Not found\"}",
                            &clientInfoRef);
  }

  // Check if it's actually a file (not a directory)
  if (S_ISDIR(fileStat.st_mode)) {
    // Path is a directory, not a file
    return sendJsonResponse(req, 404, "{\"error\":\"Not found\"}",
                            &clientInfoRef);
  }

  // Open file
  FILE *file = fopen(fullPath.c_str(), "rb");
  if (!file) {
    // Check if this is an SD card error
    if (fsAvailable && filePath.rfind("/sd/", 0) == 0) {
      handleSDCardError();
    }
    return sendJsonResponse(req, 500, "{\"error\":\"Failed to open file\"}",
                            &clientInfoRef);
  }

  // Get file size
  if (fseek(file, 0, SEEK_END) != 0) {
    // Check if this is an SD card error (errno 5 = EIO)
    if (fsAvailable && filePath.rfind("/sd/", 0) == 0 && errno == 5) {
      fclose(file);
      handleSDCardError();
      return sendJsonResponse(req, 503, "{\"error\":\"SD card unavailable\"}",
                              &clientInfoRef);
    }
    fclose(file);
    return sendJsonResponse(req, 500, "{\"error\":\"Failed to read file\"}",
                            &clientInfoRef);
  }

  long fileSize = ftell(file);
  if (fileSize < 0) {
    printf("ftell failed, errno: %d\n", errno);
    fclose(file);
    return sendJsonResponse(req, 500, "{\"error\":\"Failed to read file\"}",
                            &clientInfoRef);
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    // Check if this is an SD card error (errno 5 = EIO)
    if (fsAvailable && filePath.rfind("/sd/", 0) == 0 && errno == 5) {
      fclose(file);
      handleSDCardError();
      return sendJsonResponse(req, 503, "{\"error\":\"SD card unavailable\"}",
                              &clientInfoRef);
    }
    printf("fseek SEEK_SET failed, errno: %d\n", errno);
    fclose(file);
    return sendJsonResponse(req, 500, "{\"error\":\"Failed to read file\"}",
                            &clientInfoRef);
  }

  if (fileSize == 0) {
    // Empty file - return empty response instead of 404
    // This allows empty log files to be served (they'll get content as logs are
    // written)
    fclose(file);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "200 OK");
    esp_err_t ret = httpd_resp_send(req, "", 0);
    if (ret == ESP_OK) {
      logAccess(200, clientInfoRef, 0);
    }
    return ret;
  }

  // Determine content type based on file extension (ignore query string)
  std::string contentType = getContentType(filePath);
  httpd_resp_set_type(req, contentType.c_str());

  // Use chunked encoding for all files to allow yields between chunks.
  // IMPORTANT: Avoid dynamic allocations here (BT + TLS can make heap tight).
  // Keep the buffer small and on-stack.
  constexpr size_t CHUNK_SIZE = 2048;

  esp_err_t ret = ESP_OK;

  // Enable buffered I/O for better performance (no extra heap from us).
  setvbuf(file, nullptr, _IOFBF, CHUNK_SIZE);

  char buffer[CHUNK_SIZE];

  size_t totalSent = 0;

  // Stream file in chunks with frequent yields to prevent watchdog timeout
  while (totalSent < (size_t)fileSize && ret == ESP_OK) {
    feedWatchDog(); // Yield before each chunk
    size_t toRead = (fileSize - totalSent < CHUNK_SIZE) ? (fileSize - totalSent)
                                                        : CHUNK_SIZE;

    size_t bytesRead = fread(buffer, 1, toRead, file);
    if (bytesRead == 0) {
      // Check if this is an SD card error (errno 5 = EIO)
      if (fsAvailable && filePath.rfind("/sd/", 0) == 0 && errno == 5) {
        fclose(file);
        handleSDCardError();
        return sendJsonResponse(req, 503, "{\"error\":\"SD card unavailable\"}",
                                &clientInfoRef);
      }
      printf("fread returned 0, expected %zu bytes\n", toRead);
      ret = ESP_FAIL;
      break;
    }
    feedWatchDog(); // Yield after file I/O

    // Send chunk
    ret = httpd_resp_send_chunk(req, buffer, bytesRead);
    if (ret != ESP_OK) {
      printf("httpd_resp_send_chunk failed at %zu bytes, error: %s\n",
             totalSent, esp_err_to_name(ret));
      break;
    }
    feedWatchDog(); // Yield after network I/O

    totalSent += bytesRead;
  }

  // Finalize chunked transfer
  if (ret == ESP_OK) {
    feedWatchDog();                               // Yield before finalizing
    ret = httpd_resp_send_chunk(req, nullptr, 0); // End chunked transfer
    if (ret != ESP_OK) {
      printf("Failed to finalize chunked transfer: %s\n", esp_err_to_name(ret));
    }
    feedWatchDog(); // Yield after finalizing
  }

  fclose(file);

  if (ret != ESP_OK || totalSent != (size_t)fileSize) {
    printf("File send incomplete: sent %zu of %ld bytes\n", totalSent,
           fileSize);
    // At this point headers/body may already be partially sent; best-effort
    // log.
    logAccess(500, clientInfoRef, totalSent);
    return ret;
  }

  // Default response is 200 OK for file responses
  logAccess(200, clientInfoRef, totalSent);
  return ESP_OK;
}
