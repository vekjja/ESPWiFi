// WebServer.cpp - PsychicHttp Server Implementation
#include "ESPWiFi.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
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

#include "PsychicHttp.h"

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

bool ESPWiFi::setTlsServerCredentials(const char *certPem, size_t certPemLen,
                                      const char *keyPem, size_t keyPemLen) {
  if (certPem == nullptr || keyPem == nullptr || certPemLen == 0 ||
      keyPemLen == 0) {
    clearTlsServerCredentials();
    return false;
  }

  tlsServerCertPem_.assign(certPem, certPemLen);
  tlsServerKeyPem_.assign(keyPem, keyPemLen);

  // Basic sanity checks (best-effort; do not parse in runtime path).
  if (tlsServerCertPem_.find("BEGIN CERTIFICATE") == std::string::npos ||
      tlsServerKeyPem_.find("BEGIN") == std::string::npos) {
    clearTlsServerCredentials();
    return false;
  }

  return true;
}

void ESPWiFi::clearTlsServerCredentials() {
  tlsServerCertPem_.clear();
  tlsServerKeyPem_.clear();
}

static bool loadTlsCredentialsFromLfs(ESPWiFi *self) {
  if (self == nullptr) {
    return false;
  }
  if (self->lfs == nullptr) {
    return false;
  }

  // We store TLS materials on LittleFS so they can be uploaded/rotated without
  // rebuilding firmware. These paths are relative to LittleFS root.
  const std::string certRel = "/tls/server.crt";
  const std::string keyRel = "/tls/server.key";

  const std::string certFull = self->lfsMountPoint + certRel;
  const std::string keyFull = self->lfsMountPoint + keyRel;

  if (!self->fileExists(certFull) || !self->fileExists(keyFull)) {
    return false;
  }

  size_t certLen = 0;
  char *cert = self->readFile(certRel, &certLen);
  if (cert == nullptr || certLen == 0) {
    if (cert) {
      free(cert);
    }
    return false;
  }

  size_t keyLen = 0;
  char *key = self->readFile(keyRel, &keyLen);
  if (key == nullptr || keyLen == 0) {
    free(cert);
    if (key) {
      free(key);
    }
    return false;
  }

  // Keep in memory for the lifetime of the HTTPS server.
  const bool ok = self->setTlsServerCredentials(cert, certLen, key, keyLen);
  free(cert);
  free(key);
  return ok;
}

void ESPWiFi::startWebServer() {
  if (webServerStarted) {
    return;
  }

  // Prefer HTTPS when TLS credentials are available on LittleFS.
  tlsServerEnabled_ = false;
  webServerPort_ = 80;

  const bool haveTls = loadTlsCredentialsFromLfs(this);
  if (haveTls) {
    webServerPort_ = 443;
    tlsServerEnabled_ = true;
  } else {
    webServerPort_ = 80;
  }

  // Create PsychicHttp server with port
  webServer = new PsychicHttpServer(webServerPort_);
  webServerUserData =
      this; // Store for access in handlers (via lambda captures)

  if (haveTls) {
    webServer->setCertificate(tlsServerCertPem_.c_str(),
                              tlsServerKeyPem_.c_str());
  }

  esp_err_t ret = webServer->start();
  if (ret != ESP_OK) {
    log(ERROR, "❌ Failed to start %s server: %s",
        tlsServerEnabled_ ? "HTTPS" : "HTTP", esp_err_to_name(ret));
    delete webServer;
    webServer = nullptr;
    webServerStarted = false;
    return;
  }

  webServerStarted = true;

  // Global CORS preflight handler (covers all routes)
  webServer->on("*", HTTP_OPTIONS,
                [this](PsychicRequest *request, PsychicResponse *response) {
                  addCORS(request);
                  response->send(200);
                  return ESP_OK;
                });

  // Restart endpoint
  webServer->on("/api/restart", HTTP_POST,
                [this](PsychicRequest *request, PsychicResponse *response) {
                  response->send(200, "application/json",
                                 "{\"status\":\"restarting\"}");
                  vTaskDelay(pdMS_TO_TICKS(500));
                  esp_restart();
                  return ESP_OK;
                });

  log(INFO, "🗄️ %s Web Server started", tlsServerEnabled_ ? "HTTPS" : "HTTP");
  const char *scheme = tlsServerEnabled_ ? "https" : "http";
  log(INFO, "🗄️\t%s://%s:%d", scheme, getHostname().c_str(), webServerPort_);
  log(INFO, "🗄️\t%s://%s:%d", scheme, ipAddress().c_str(), webServerPort_);
  if (config["wifi"]["mdns"].as<bool>()) {
    log(INFO, "🗄️\t%s://%s.local:%d", scheme, getHostname().c_str(),
        webServerPort_);
  }
}

esp_err_t ESPWiFi::registerRoute(const char *uri, httpd_method_t method,
                                 PsychicRouteHandler handler) {
  if (!webServer) {
    log(ERROR, "Cannot register route %s: web server not initialized",
        (uri != nullptr) ? uri : "(null)");
    return ESP_ERR_INVALID_STATE;
  }
  if (uri == nullptr || !handler) {
    return ESP_ERR_INVALID_ARG;
  }

  // Wrap handler with auth/CORS verification
  ESPWiFi *self = this;
  auto wrappedHandler = [self,
                         handler](PsychicRequest *request,
                                  PsychicResponse *response) -> esp_err_t {
    // Verify request (auth + CORS)
    std::string clientInfo;
    if (self->verifyRequest(request, &clientInfo) != ESP_OK) {
      return ESP_OK; // preflight or error already handled
    }

    // Call user handler (it returns esp_err_t, we need to adapt)
    esp_err_t ret = handler(request);
    return ret;
  };

  PsychicEndpoint *endpoint = webServer->on(uri, method, wrappedHandler);
  esp_err_t ret = (endpoint != nullptr) ? ESP_OK : ESP_FAIL;
  if (ret != ESP_OK) {
    log(ERROR, "Failed to register route %s: %s", uri, esp_err_to_name(ret));
    return ret;
  }

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

std::string ESPWiFi::getClientInfo(PsychicRequest *request) {
  if (request == nullptr) {
    return std::string("- - - -");
  }

  const char *method = getMethodString(request->method());
  String uri = request->uri();
  String userAgent = request->header("User-Agent");
  if (userAgent.length() == 0) {
    userAgent = "-";
  }
  IPAddress ip = request->client()->remoteIP();
  String remoteIp = ip.toString();
  if (remoteIp.length() == 0) {
    remoteIp = "-";
  }

  char clientInfoBuf[1024];
  (void)snprintf(clientInfoBuf, sizeof(clientInfoBuf), "%s - %s - %s - %s",
                 remoteIp.c_str(), method, uri.c_str(), userAgent.c_str());
  return std::string(clientInfoBuf);
}

std::string ESPWiFi::getClientInfo(httpd_req_t *req) {
  if (req == nullptr) {
    return std::string("- - - -");
  }

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

esp_err_t ESPWiFi::sendJsonResponse(PsychicRequest *request, int statusCode,
                                    const std::string &jsonBody) {
  if (request == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  std::string clientInfo = getClientInfo(request);
  PsychicResponse *response = request->response();
  response->setCode(statusCode);
  response->setContentType("application/json");
  response->setContent(jsonBody.c_str());
  esp_err_t ret = response->send();

  size_t sent = (ret == ESP_OK) ? jsonBody.size() : 0;
  logAccess(statusCode, clientInfo, sent);
  return ret;
}

esp_err_t ESPWiFi::sendJsonResponseInternal(httpd_req_t *req, int statusCode,
                                            const std::string &jsonBody,
                                            const std::string *clientInfo) {
  if (req == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
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

esp_err_t ESPWiFi::sendFileResponse(PsychicRequest *request,
                                    const std::string &filePath) {
  if (request == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  std::string clientInfo = getClientInfo(request);

  // Resolve which filesystem this path maps to
  std::string fullPath;
  bool fsAvailable = false;
  if (filePath.rfind("/sd/", 0) == 0 || filePath == "/sd") {
    fsAvailable = (sdCard != nullptr);
    fullPath = filePath;
  } else if (filePath.rfind("/lfs/", 0) == 0 || filePath == "/lfs") {
    fsAvailable = (lfs != nullptr);
    fullPath = filePath;
  } else {
    fsAvailable = (lfs != nullptr);
    fullPath = lfsMountPoint + filePath;
  }

  if (!fsAvailable) {
    sendJsonResponse(request, 503, "{\"error\":\"Filesystem not available\"}");
    return ESP_OK;
  }

  // Check if file exists
  struct stat fileStat;
  if (stat(fullPath.c_str(), &fileStat) != 0 || S_ISDIR(fileStat.st_mode)) {
    sendJsonResponse(request, 404, "{\"error\":\"Not found\"}");
    return ESP_OK;
  }

  // Use underlying httpd_req_t for file serving (PsychicHttp doesn't have
  // direct file serving with all features like range requests)
  httpd_req_t *req = request->request();
  if (req == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return sendFileResponseInternal(req, filePath, &clientInfo);
}

esp_err_t ESPWiFi::sendFileResponseInternal(httpd_req_t *req,
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
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Not found\"}", HTTPD_RESP_USE_STRLEN);
    logAccess(404, clientInfoRef, 0);
    return ESP_OK;
  }

  // Check if it's actually a file (not a directory)
  if (S_ISDIR(fileStat.st_mode)) {
    // Path is a directory, not a file
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Not found\"}", HTTPD_RESP_USE_STRLEN);
    logAccess(404, clientInfoRef, 0);
    return ESP_OK;
  }

  // Open file
  FILE *file = fopen(fullPath.c_str(), "rb");
  if (!file) {
    // Check if this is an SD card error
    if (fsAvailable && filePath.rfind("/sd/", 0) == 0) {
      handleSDCardError();
    }
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Failed to open file\"}",
                    HTTPD_RESP_USE_STRLEN);
    logAccess(500, clientInfoRef, 0);
    return ESP_OK;
  }

  // Get file size
  if (fseek(file, 0, SEEK_END) != 0) {
    // Check if this is an SD card error (errno 5 = EIO)
    if (fsAvailable && filePath.rfind("/sd/", 0) == 0 && errno == 5) {
      fclose(file);
      handleSDCardError();
      httpd_resp_set_status(req, "503 Service Unavailable");
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"error\":\"SD card unavailable\"}",
                      HTTPD_RESP_USE_STRLEN);
      logAccess(503, clientInfoRef, 0);
      return ESP_OK;
    }
    fclose(file);
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Failed to read file\"}",
                    HTTPD_RESP_USE_STRLEN);
    logAccess(500, clientInfoRef, 0);
    return ESP_OK;
  }

  long fileSize = ftell(file);
  if (fileSize < 0) {
    printf("ftell failed, errno: %d\n", errno);
    fclose(file);
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Failed to read file\"}",
                    HTTPD_RESP_USE_STRLEN);
    logAccess(500, clientInfoRef, 0);
    return ESP_OK;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    // Check if this is an SD card error (errno 5 = EIO)
    if (fsAvailable && filePath.rfind("/sd/", 0) == 0 && errno == 5) {
      fclose(file);
      handleSDCardError();
      httpd_resp_set_status(req, "503 Service Unavailable");
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"error\":\"SD card unavailable\"}",
                      HTTPD_RESP_USE_STRLEN);
      logAccess(503, clientInfoRef, 0);
      return ESP_OK;
    }
    printf("fseek SEEK_SET failed, errno: %d\n", errno);
    fclose(file);
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Failed to read file\"}",
                    HTTPD_RESP_USE_STRLEN);
    logAccess(500, clientInfoRef, 0);
    return ESP_OK;
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

  // Check for Range header (for video/audio streaming)
  size_t rangeHeaderLen = httpd_req_get_hdr_value_len(req, "Range");
  long rangeStart = 0;
  long rangeEnd = fileSize - 1;
  bool isRangeRequest = false;

  if (rangeHeaderLen > 0 && rangeHeaderLen < 128) {
    char rangeHeader[128];
    if (httpd_req_get_hdr_value_str(req, "Range", rangeHeader,
                                    sizeof(rangeHeader)) == ESP_OK) {
      // Parse "bytes=start-end" or "bytes=start-"
      if (strncmp(rangeHeader, "bytes=", 6) == 0) {
        char *rangeStr = rangeHeader + 6;
        char *dashPos = strchr(rangeStr, '-');
        if (dashPos) {
          *dashPos = '\0';
          rangeStart = atol(rangeStr);
          if (*(dashPos + 1) != '\0') {
            rangeEnd = atol(dashPos + 1);
          }
          // Validate range
          if (rangeStart >= 0 && rangeStart < fileSize) {
            if (rangeEnd >= fileSize) {
              rangeEnd = fileSize - 1;
            }
            if (rangeEnd >= rangeStart) {
              isRangeRequest = true;
              log(DEBUG, "🗄️ Range request: bytes=%ld-%ld/%ld", rangeStart,
                  rangeEnd, fileSize);
            }
          }
        }
      }
    }
  }

  // Accept-Ranges header to indicate we support range requests
  httpd_resp_set_hdr(req, "Accept-Ranges", "bytes");

  // Seek to start position if this is a range request
  if (isRangeRequest && rangeStart > 0) {
    if (fseek(file, rangeStart, SEEK_SET) != 0) {
      fclose(file);
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_set_type(req, "application/json");
      httpd_resp_send(req, "{\"error\":\"Failed to seek file\"}",
                      HTTPD_RESP_USE_STRLEN);
      logAccess(500, clientInfoRef, 0);
      return ESP_OK;
    }
  }

  esp_err_t ret = ESP_OK;
  size_t totalSent = 0;
  size_t bytesToSend = isRangeRequest ? (rangeEnd - rangeStart + 1) : fileSize;

  // For range requests, we need to send exact bytes with Content-Length
  // For full files, we can use chunked encoding for progressive streaming
  if (isRangeRequest) {
    // Handle range request: stream in chunks using direct socket send
    // Set 206 Partial Content status
    httpd_resp_set_status(req, "206 Partial Content");

    // Set Content-Range header
    char contentRangeStr[64];
    snprintf(contentRangeStr, sizeof(contentRangeStr), "bytes %ld-%ld/%ld",
             rangeStart, rangeEnd, fileSize);
    httpd_resp_set_hdr(req, "Content-Range", contentRangeStr);

    // Set Content-Length for the range
    char contentLengthStr[32];
    snprintf(contentLengthStr, sizeof(contentLengthStr), "%zu", bytesToSend);
    httpd_resp_set_hdr(req, "Content-Length", contentLengthStr);

    log(DEBUG, "🗄️ Serving range %ld-%ld (%zu bytes) of %ld byte file",
        rangeStart, rangeEnd, bytesToSend, fileSize);

    // Get socket for direct sending
    int sockfd = httpd_req_to_sockfd(req);

    // Stream the range in chunks using direct socket send
    // Use 32KB chunks for faster streaming (MP3 players need quick initial
    // buffer)
    constexpr size_t CHUNK_SIZE = 32768;
    char *buffer = (char *)malloc(CHUNK_SIZE);
    if (!buffer) {
      fclose(file);
      log(ERROR, "Out of memory allocating %zu byte buffer", CHUNK_SIZE);
      return sendJsonResponseInternal(req, 500, "{\"error\":\"Out of memory\"}",
                                      &clientInfoRef);
    }

    while (totalSent < bytesToSend && ret == ESP_OK) {
      size_t toRead = (bytesToSend - totalSent < CHUNK_SIZE)
                          ? (bytesToSend - totalSent)
                          : CHUNK_SIZE;

      size_t bytesRead = fread(buffer, 1, toRead, file);
      if (bytesRead == 0) {
        if (fsAvailable && filePath.rfind("/sd/", 0) == 0 && errno == 5) {
          free(buffer);
          fclose(file);
          handleSDCardError();
          logAccess(503, clientInfoRef, totalSent);
          return ESP_FAIL;
        }
        log(ERROR,
            "Failed to read range: got %zu bytes, expected %zu at offset %zu",
            bytesRead, toRead, totalSent);
        ret = ESP_FAIL;
        break;
      }

      // Send directly to socket (no chunked encoding)
      ssize_t sent =
          httpd_socket_send(req->handle, sockfd, buffer, bytesRead, 0);
      if (sent < 0) {
        log(ERROR, "Failed to send range chunk at %zu bytes: error %d",
            totalSent, (int)sent);
        ret = ESP_FAIL;
        break;
      }

      // Feed watchdog once per chunk (after send completes)
      feedWatchDog();

      totalSent += bytesRead;
    }

    free(buffer);
    fclose(file);
    log(DEBUG, "🗄️ Range request completed: %s (sent %zu bytes)",
        (ret == ESP_OK) ? "OK" : "FAILED", totalSent);
    logAccess((ret == ESP_OK) ? 206 : 500, clientInfoRef, totalSent);
    return ret;
  } else {
    // Full file request: use chunked encoding for progressive streaming
    httpd_resp_set_status(req, "200 OK");

    log(ACCESS, "🗄️ Serving File: %s (%ld bytes) using chunked encoding",
        filePath.c_str(), fileSize);

    // Use smaller chunk size to reduce memory pressure (2KB instead of 4KB)
    // Allocate from heap (not stack) to ensure it's in regular RAM, not
    // PSRAM
    constexpr size_t CHUNK_SIZE = 2048;
    char *buffer = (char *)malloc(CHUNK_SIZE);
    if (!buffer) {
      fclose(file);
      log(ERROR, "Out of memory allocating %zu byte buffer", CHUNK_SIZE);
      return sendJsonResponseInternal(req, 500, "{\"error\":\"Out of memory\"}",
                                      &clientInfoRef);
    }

    while (totalSent < bytesToSend && ret == ESP_OK) {
      feedWatchDog();
      size_t toRead = (bytesToSend - totalSent < CHUNK_SIZE)
                          ? (bytesToSend - totalSent)
                          : CHUNK_SIZE;

      size_t bytesRead = fread(buffer, 1, toRead, file);
      if (bytesRead == 0) {
        if (fsAvailable && filePath.rfind("/sd/", 0) == 0 && errno == 5) {
          free(buffer);
          fclose(file);
          handleSDCardError();
          httpd_resp_set_status(req, "503 Service Unavailable");
          httpd_resp_set_type(req, "application/json");
          httpd_resp_send(req, "{\"error\":\"SD card unavailable\"}",
                          HTTPD_RESP_USE_STRLEN);
          logAccess(503, clientInfoRef, totalSent);
          return ESP_OK;
        }
        log(ERROR,
            "fread returned 0, expected %zu bytes (sent %zu of %zu so far)",
            toRead, totalSent, bytesToSend);
        ret = ESP_FAIL;
        break;
      }
      feedWatchDog();

      ret = httpd_resp_send_chunk(req, buffer, bytesRead);
      if (ret != ESP_OK) {
        log(ERROR, "httpd_resp_send_chunk failed at %zu bytes: %s", totalSent,
            esp_err_to_name(ret));
        break;
      }
      feedWatchDog();

      totalSent += bytesRead;
    }

    // Finalize chunked transfer
    if (ret == ESP_OK) {
      feedWatchDog();
      ret = httpd_resp_send_chunk(req, nullptr, 0);
      if (ret != ESP_OK) {
        log(ERROR, "Failed to finalize chunked transfer: %s",
            esp_err_to_name(ret));
      } else {
        log(ACCESS, "🗄️ File transfer completed: %s (%zu bytes)",
            filePath.c_str(), totalSent);
      }
      feedWatchDog();
    }

    free(buffer);
    fclose(file);
    logAccess((ret == ESP_OK) ? 200 : 500, clientInfoRef, totalSent);
    return ret;
  }
}
