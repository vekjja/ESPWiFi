#include <WebSocket.h>

#include <ESPWiFi.h>

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <new>
#include <string>
#include <sys/socket.h>

#include "esp_websocket_client.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_timer.h"

struct WebSocket::BroadcastJob {
  WebSocket *ws;
  httpd_ws_type_t type;
  size_t len;
  // payload bytes follow
};

// -------------------------
// Cloud tunnel helpers
// -------------------------

// Global dial mutex to avoid multiple TLS handshakes starting simultaneously.
// TLS setup can have a high transient heap cost; parallel handshakes can cause
// MBEDTLS_ERR_SSL_ALLOC_FAILED (-0x7F00) even though each tunnel would succeed
// on its own.
static SemaphoreHandle_t s_cloudDialMu = nullptr;
static portMUX_TYPE s_cloudDialMux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t cloudDialMu() {
  if (s_cloudDialMu == nullptr) {
    // Guard creation against races across multiple wsCloud tasks.
    portENTER_CRITICAL(&s_cloudDialMux);
    if (s_cloudDialMu == nullptr) {
      s_cloudDialMu = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_cloudDialMux);
  }
  return s_cloudDialMu;
}

// Extract a JSON string field from a small, trusted broker message without
// pulling in ArduinoJson on the esp_websocket_client internal task stack.
// This is a minimal parser that looks for: "<key>" : "<value>"
static bool jsonGetStringField(const char *json, size_t jsonLen,
                               const char *key, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return false;
  }
  out[0] = '\0';
  if (json == nullptr || key == nullptr || key[0] == '\0') {
    return false;
  }
  // Build needle: "<key>"
  char needle[64];
  const size_t keyLen = strnlen(key, sizeof(needle));
  // Need: '"' + key + '"' + '\0'
  if (keyLen + 3 > sizeof(needle)) {
    return false;
  }
  needle[0] = '"';
  memcpy(&needle[1], key, keyLen);
  needle[1 + keyLen] = '"';
  needle[2 + keyLen] = '\0';

  const char *hay = json;
  const char *end = json + jsonLen;
  const size_t needleLen = strlen(needle);
  if (needleLen == 0) {
    return false;
  }
  for (const char *p = hay; p + needleLen < end; p++) {
    if (memcmp(p, needle, needleLen) != 0) {
      continue;
    }
    p += needleLen;
    // skip whitespace
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
      p++;
    }
    if (p >= end || *p != ':') {
      continue;
    }
    p++; // skip ':'
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
      p++;
    }
    if (p >= end || *p != '"') {
      continue;
    }
    p++; // start of value
    size_t oi = 0;
    while (p < end && *p != '"' && oi + 1 < outLen) {
      const char c = *p++;
      // Broker-controlled values are plain URLs/tokens/types (no escapes).
      if (c == '\\') {
        // Skip basic escape sequences defensively.
        if (p < end) {
          out[oi++] = *p++;
        }
      } else {
        out[oi++] = c;
      }
    }
    out[oi] = '\0';
    return (oi > 0);
  }
  return false;
}

static size_t safeCopy(char *dst, size_t dstLen, const char *src) {
  if (dst == nullptr || dstLen == 0) {
    return 0;
  }
  dst[0] = '\0';
  if (src == nullptr) {
    return 0;
  }
  size_t i = 0;
  for (; i + 1 < dstLen && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return i;
}

static size_t safeAppend(char *dst, size_t dstLen, const char *src) {
  if (dst == nullptr || dstLen == 0) {
    return 0;
  }
  if (src == nullptr || src[0] == '\0') {
    return strlen(dst);
  }
  size_t di = strnlen(dst, dstLen);
  if (di >= dstLen) {
    // Not NUL-terminated (shouldn't happen); force terminate.
    dst[dstLen - 1] = '\0';
    return dstLen - 1;
  }
  size_t si = 0;
  while (di + 1 < dstLen && src[si] != '\0') {
    dst[di++] = src[si++];
  }
  dst[di] = '\0';
  return di;
}

static bool isUnreservedUrlChar(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return true;
  }
  return (c == '-' || c == '_' || c == '.' || c == '~');
}

static size_t urlEncode(const char *in, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return 0;
  }
  out[0] = '\0';
  if (in == nullptr) {
    return 0;
  }
  static const char *hex = "0123456789ABCDEF";
  size_t oi = 0;
  for (size_t i = 0; in[i] != '\0'; i++) {
    const unsigned char c = (unsigned char)in[i];
    if (isUnreservedUrlChar((char)c)) {
      if (oi + 1 >= outLen) {
        break;
      }
      out[oi++] = (char)c;
    } else {
      if (oi + 3 >= outLen) {
        break;
      }
      out[oi++] = '%';
      out[oi++] = hex[c >> 4];
      out[oi++] = hex[c & 0x0F];
    }
  }
  out[oi] = '\0';
  return oi;
}

static void deriveTunnelKeyFromUri(const char *uri, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return;
  }
  out[0] = '\0';
  if (uri == nullptr) {
    return;
  }
  // Example: "/ws/camera" -> "ws_camera"
  size_t oi = 0;
  for (size_t i = 0; uri[i] != '\0' && oi + 1 < outLen; i++) {
    const char c = uri[i];
    if (c == '/') {
      // skip leading '/', but translate internal '/' to '_'
      if (i == 0) {
        continue;
      }
      out[oi++] = '_';
    } else if (std::isalnum((unsigned char)c) || c == '-' || c == '_' ||
               c == '.') {
      out[oi++] = (char)std::tolower((unsigned char)c);
    } else {
      // replace any weird chars with '_'
      out[oi++] = '_';
    }
  }
  out[oi] = '\0';
}

esp_err_t WebSocket::cloudSendFrame_(httpd_ws_type_t type,
                                     const uint8_t *payload, size_t len) {
  if (!cloudTunnelConnected_ || !cloudTunnelEnabled_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (payload == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }

  // Lock around client handle usage so stop/destroy can't race send.
  SemaphoreHandle_t mu = (SemaphoreHandle_t)cloudMutex_;
  const bool locked =
      (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(25)) == pdTRUE);
  esp_websocket_client_handle_t c = (esp_websocket_client_handle_t)cloudClient_;
  const bool connected = (c != nullptr) && esp_websocket_client_is_connected(c);
  int sent = -1;
  if (connected) {
    // Default: never block in broadcast path.
    // For camera binary frames, allow a small timeout to reduce disconnects due
    // to transient backpressure over the internet tunnel.
    const bool isCamera = (strcmp(uri_, "/ws/camera") == 0);
    const bool isControl = (strcmp(uri_, "/ws/control") == 0);
    const int timeoutMs =
        (isCamera && type != HTTPD_WS_TYPE_TEXT) ? 200 : (isControl ? 200 : 0);
    if (type == HTTPD_WS_TYPE_TEXT) {
      sent = esp_websocket_client_send_text(c, (const char *)payload, (int)len,
                                            timeoutMs);
    } else {
      sent = esp_websocket_client_send_bin(c, (const char *)payload, (int)len,
                                           timeoutMs);
    }
  }
  if (locked) {
    (void)xSemaphoreGive(mu);
  }
  return (sent >= 0) ? ESP_OK : ESP_FAIL;
}

void WebSocket::getRemoteInfo_(int fd, char *outIp, size_t outIpLen,
                               uint16_t *outPort) {
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (outIp && outIpLen > 0) {
    outIp[0] = '\0';
  }
  if (outPort) {
    *outPort = 0;
  }
  if (fd < 0 || outIp == nullptr || outIpLen == 0) {
    return;
  }

  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) != 0) {
    return;
  }

  if (addr.ss_family == AF_INET) {
    struct sockaddr_in *a = (struct sockaddr_in *)&addr;
    (void)inet_ntop(AF_INET, &a->sin_addr, outIp, outIpLen);
    if (outPort) {
      *outPort = ntohs(a->sin_port);
    }
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
      (void)inet_ntop(AF_INET, &v4, outIp, outIpLen);
    } else {
      (void)inet_ntop(AF_INET6, &a6->sin6_addr, outIp, outIpLen);
    }
    if (outPort) {
      *outPort = ntohs(a6->sin6_port);
    }
  }
#else
  (void)fd;
  (void)outIp;
  (void)outIpLen;
  (void)outPort;
#endif
}

esp_err_t WebSocket::wsHandlerTrampoline(httpd_req_t *req) {
  if (req == nullptr || req->user_ctx == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return static_cast<WebSocket *>(req->user_ctx)->handleWsRequest(req);
}

void WebSocket::addClient_(int fd) {
  if (fd < 0) {
    return;
  }
  for (size_t i = 0; i < clientCount_; ++i) {
    if (clientFds_[i] == fd) {
      return; // already tracked
    }
  }
  if (clientCount_ >= kMaxClients) {
    // Drop oldest to stay bounded; better to keep things moving than to fail.
    for (size_t i = 1; i < kMaxClients; ++i) {
      clientFds_[i - 1] = clientFds_[i];
    }
    clientFds_[kMaxClients - 1] = fd;
    return;
  }
  clientFds_[clientCount_++] = fd;
}

void WebSocket::removeClient_(int fd) {
  for (size_t i = 0; i < clientCount_; ++i) {
    if (clientFds_[i] == fd) {
      for (size_t j = i + 1; j < clientCount_; ++j) {
        clientFds_[j - 1] = clientFds_[j];
      }
      clientCount_--;
      return;
    }
  }
}

esp_err_t WebSocket::handleWsRequest(httpd_req_t *req) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)req;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (espWifi_ == nullptr || espWifi_->webServer == nullptr || req == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  // In ESP-IDF's httpd, WebSocket handshake comes in as HTTP_GET.
  //
  // IMPORTANT: For websocket URIs, ESP-IDF sends the 101 Switching Protocols
  // response *before* invoking this handler (see httpd_uri.c). Do NOT call
  // any httpd_resp_* APIs here (including CORS/auth helpers), or you'll write
  // plain HTTP bytes onto an upgraded websocket connection and clients may
  // fail with errors like "Invalid WebSocket frame: RSV1 must be clear".
  if (req->method == HTTP_GET) {
    const int fd = httpd_req_to_sockfd(req);

    // Optional auth gate (websocket handshake already upgraded; do NOT try to
    // send 401 here). Instead, immediately close the session if unauthorized.
    if (requireAuth_ && espWifi_->authEnabled() &&
        !espWifi_->isExcludedPath(req->uri)) {
      bool ok = espWifi_->authorized(req);
      if (!ok) {
        // Browser WebSocket APIs can't set Authorization headers. Allow token
        // via query param: ws://host/path?token=...
        const std::string tok = espWifi_->getQueryParam(req, "token");
        const char *expectedC =
            espWifi_->config["auth"]["token"].as<const char *>();
        const std::string expected = (expectedC != nullptr) ? expectedC : "";
        ok = (!tok.empty() && !expected.empty() && tok == expected);
      }
      if (!ok) {
        espWifi_->log(WARNING, "🔒 WS(%s) unauthorized; closing (fd=%d)", uri_,
                      fd);
        // Best-effort: terminate the session.
        (void)httpd_sess_trigger_close(espWifi_->webServer, fd);
        return ESP_OK;
      }
    }

    addClient_(fd);
    {
      char ip[64];
      uint16_t port = 0;
      getRemoteInfo_(fd, ip, sizeof(ip), &port);
      if (ip[0] != '\0') {
        espWifi_->log(INFO, "🕸️ WebSocket Client Connected: %s 🔗", uri_);
        espWifi_->log(DEBUG, "🕸️\tFD: %d", fd);
        espWifi_->log(DEBUG, "🕸️\tIP: %s", ip);
        espWifi_->log(DEBUG, "🕸️\tPort: %u", (unsigned)port);
      } else {
        espWifi_->log(INFO, "🕸️ WebSocket Client Connected: %s 🔗 (fd=%d)",
                      uri_, fd);
      }
    }
    if (onConnect_) {
      onConnect_(this, fd, espWifi_);
    }
    return ESP_OK;
  }

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));

  // First call: just get length/type.
  esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  const int fd = httpd_req_to_sockfd(req);

  if (frame.type == HTTPD_WS_TYPE_CLOSE) {
    // Client initiated close
    espWifi_->log(INFO, "🕸️ WebSocket Client Disconnected: %s (fd=%d)", uri_,
                  fd);
    espWifi_->log(DEBUG, "🕸️\tDisconnect Time: %llu ms",
                  (unsigned long long)(esp_timer_get_time() / 1000ULL));

    removeClient_(fd);
    if (onDisconnect_) {
      onDisconnect_(this, fd, espWifi_);
    }
    return ESP_OK;
  }

  if (frame.len > maxMessageLen_) {
    // Too large to buffer safely (bounded RAM). Returning failure will cause
    // the server to close/cleanup the session.
    espWifi_->log(WARNING, "WS(%s) rx too large: %u > %u", uri_,
                  (unsigned)frame.len, (unsigned)maxMessageLen_);
    return ESP_FAIL;
  }

  // Stack buffer for typical small messages; fall back to heap for larger ones
  // (still bounded by maxMessageLen_).
  uint8_t stackBuf[256];
  std::unique_ptr<uint8_t[]> heapBuf;
  uint8_t *buf = nullptr;
  if (frame.len <= sizeof(stackBuf)) {
    buf = stackBuf;
  } else if (frame.len > 0) {
    heapBuf.reset(new (std::nothrow) uint8_t[frame.len]);
    buf = heapBuf.get();
  }

  if (frame.len > 0 && buf == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  if (frame.len > 0) {
    frame.payload = buf;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
      return ret;
    }
  }

  if (onMessage_) {
    onMessage_(this, fd, frame.type,
               (frame.len > 0) ? (const uint8_t *)frame.payload : nullptr,
               frame.len, espWifi_);
  }

  return ESP_OK;
#endif
}

void WebSocket::broadcastWorkTrampoline(void *arg) {
  if (arg == nullptr) {
    return;
  }
  BroadcastJob *job = static_cast<BroadcastJob *>(arg);
  WebSocket *ws = job->ws;
  const uint8_t *payload = (const uint8_t *)(job + 1);
  if (ws != nullptr) {
    ws->broadcastNow_(job->type, payload, job->len);
  }
  free(job);
}

void WebSocket::broadcastNow_(httpd_ws_type_t type, const uint8_t *payload,
                              size_t len) {
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (espWifi_ == nullptr || espWifi_->webServer == nullptr) {
    return;
  }
  httpd_handle_t hd = espWifi_->webServer;

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = type;
  frame.payload = (uint8_t *)payload;
  frame.len = len;

  // Iterate our own tracked fds. On send failure, drop the fd to keep the
  // list clean and future broadcasts fast.
  for (size_t i = 0; i < clientCount_;) {
    int fd = clientFds_[i];
    esp_err_t err = httpd_ws_send_frame_async(hd, fd, &frame);
    if (err != ESP_OK) {
      espWifi_->log(
          INFO,
          "🕸️ WebSocket Client Disconnected: %s (fd=%d) ⛓️‍💥",
          uri_, fd);
      if (onDisconnect_) {
        onDisconnect_(this, fd, espWifi_);
      }
      removeClient_(fd);
      continue; // don't increment i; removeClient_ shifts
    }
    ++i;
  }

  // Also forward broadcasts to the cloud tunnel if enabled/connected.
  if (cloudTunnelEnabled_ && cloudTunnelConnected_) {
    // Keep payload bounded using the existing maxBroadcastLen_ enforcement at
    // the call-sites (textAll/binaryAll). This is a last line of defense.
    if (len <= maxBroadcastLen_) {
      (void)cloudSendFrame_(type, payload, len);
    }
  }
#else
  (void)type;
  (void)payload;
  (void)len;
#endif
}

WebSocket::~WebSocket() {
  stopCloudTunnel_();
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (started_ && espWifi_ != nullptr && espWifi_->webServer != nullptr &&
      uri_[0] != '\0') {
    // Best-effort cleanup; ignore errors (server may already be stopped).
    (void)httpd_unregister_uri_handler(espWifi_->webServer, uri_, HTTP_GET);
  }
#endif
}

bool WebSocket::begin(const char *uri, ESPWiFi *espWifi, OnMessageCb onMessage,
                      OnConnectCb onConnect, OnDisconnectCb onDisconnect,
                      size_t maxMessageLen, size_t maxBroadcastLen,
                      bool requireAuth) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)uri;
  (void)espWifi;
  (void)onMessage;
  (void)onConnect;
  (void)onDisconnect;
  (void)maxMessageLen;
  (void)maxBroadcastLen;
  (void)requireAuth;
  return false;
#else
  if (uri == nullptr || espWifi == nullptr) {
    return false;
  }

  espWifi_ = espWifi;
  onMessage_ = onMessage;
  onConnect_ = onConnect;
  onDisconnect_ = onDisconnect;
  requireAuth_ = requireAuth;

  // Keep limits sane and bounded.
  maxMessageLen_ = (maxMessageLen == 0) ? 1 : maxMessageLen;
  if (maxMessageLen_ > 8192) {
    maxMessageLen_ = 8192; // hard cap to prevent pathological allocations
  }
  maxBroadcastLen_ = (maxBroadcastLen == 0) ? 1 : maxBroadcastLen;
  if (maxBroadcastLen_ > 262144) {
    maxBroadcastLen_ = 262144; // hard cap to prevent pathological allocations
  }

  // Copy URI (truncate if needed).
  uri_[0] = '\0';
  (void)snprintf(uri_, sizeof(uri_), "%s", uri);

  // Ensure HTTP server is running.
  espWifi_->startWebServer();
  if (espWifi_->webServer == nullptr) {
    return false;
  }

  httpd_uri_t wsUri = {
      .uri = uri_,
      .method = HTTP_GET,
      .handler = &WebSocket::wsHandlerTrampoline,
      .user_ctx = this,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = nullptr,
  };

  esp_err_t err = httpd_register_uri_handler(espWifi_->webServer, &wsUri);
  if (err != ESP_OK) {
    espWifi_->log(ERROR, "🕸️ WebSocket(%s) register failed: %s", uri_,
                  esp_err_to_name(err));
    started_ = false;
    return false;
  }

  started_ = true;
  espWifi_->log(INFO, "🕸️ WebSocket Started: %s", uri_);

  // Optional: auto-configure cloud tunnel from ESPWiFi config.
  applyCloudTunnelConfigFromESPWiFi_();
  return true;
#endif
}

void WebSocket::setCloudTunnelEnabled(bool enabled) {
  if (!enabled) {
    cloudTunnelEnabled_ = false;
    stopCloudTunnel_();
    return;
  }
  cloudTunnelEnabled_ = true;
  startCloudTunnelTask_();
}

void WebSocket::configureCloudTunnel(const char *baseUrl, const char *deviceId,
                                     const char *token, const char *tunnelKey) {
  // Build candidate values first so we can detect no-op updates.
  char nextBase[sizeof(cloudBaseUrl_)] = {0};
  char nextDev[sizeof(cloudDeviceId_)] = {0};
  char nextTok[sizeof(cloudToken_)] = {0};
  char nextKey[sizeof(cloudTunnelKey_)] = {0};

  safeCopy(nextBase, sizeof(nextBase), baseUrl ? baseUrl : "");
  if (deviceId != nullptr && deviceId[0] != '\0') {
    safeCopy(nextDev, sizeof(nextDev), deviceId);
  } else if (espWifi_ != nullptr) {
    const std::string h = espWifi_->getHostname();
    safeCopy(nextDev, sizeof(nextDev), h.c_str());
  }
  safeCopy(nextTok, sizeof(nextTok), token ? token : "");
  if (tunnelKey != nullptr && tunnelKey[0] != '\0') {
    safeCopy(nextKey, sizeof(nextKey), tunnelKey);
  } else {
    deriveTunnelKeyFromUri(uri_, nextKey, sizeof(nextKey));
  }

  const bool unchanged = (strcmp(nextBase, cloudBaseUrl_) == 0) &&
                         (strcmp(nextDev, cloudDeviceId_) == 0) &&
                         (strcmp(nextTok, cloudToken_) == 0) &&
                         (strcmp(nextKey, cloudTunnelKey_) == 0);

  // Apply values (even if unchanged, safeCopy is cheap; but avoid restarting).
  safeCopy(cloudBaseUrl_, sizeof(cloudBaseUrl_), nextBase);
  safeCopy(cloudDeviceId_, sizeof(cloudDeviceId_), nextDev);
  safeCopy(cloudToken_, sizeof(cloudToken_), nextTok);
  safeCopy(cloudTunnelKey_, sizeof(cloudTunnelKey_), nextKey);

  // If already enabled, restart only when settings actually changed.
  if (cloudTunnelEnabled_ && !unchanged) {
    // Stop the current client (keep enabled flag true so the task reconnects).
    cloudTunnelConnected_ = false;
    SemaphoreHandle_t mu = (SemaphoreHandle_t)cloudMutex_;
    const bool locked =
        (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(200)) == pdTRUE);
    esp_websocket_client_handle_t c =
        (esp_websocket_client_handle_t)cloudClient_;
    cloudClient_ = nullptr;
    if (c) {
      (void)esp_websocket_client_stop(c);
      esp_websocket_client_destroy(c);
    }
    if (locked) {
      (void)xSemaphoreGive(mu);
    }
    startCloudTunnelTask_();
  }
}

void WebSocket::applyCloudTunnelConfigFromESPWiFi_() {
  if (espWifi_ == nullptr) {
    return;
  }
  // Config schema:
  // cloudTunnel.enabled (bool)
  // cloudTunnel.baseUrl (string)
  // cloudTunnel.tunnelAll (bool)
  // cloudTunnel.uris (array<string>) - if non-empty, only these URIs tunnel
  const bool enabled =
      espWifi_->config["cloudTunnel"]["enabled"].isNull()
          ? false
          : espWifi_->config["cloudTunnel"]["enabled"].as<bool>();
  const char *baseUrl =
      espWifi_->config["cloudTunnel"]["baseUrl"].as<const char *>();
  // Use the shared auth token so both device and UI can use the same secret.
  // If cloud tunnel is enabled but token is empty, mint one and persist it so
  // the dashboard can use it too.
  const char *tokenC = espWifi_->config["auth"]["token"].as<const char *>();
  std::string token = (tokenC != nullptr) ? std::string(tokenC) : "";
  if (enabled && token.empty()) {
    token = espWifi_->generateToken();
    espWifi_->config["auth"]["token"] = token;
    // Defer flash write to the main loop (watchdog-safe) [[memory:12698303]]
    espWifi_->requestConfigSave();
    espWifi_->log(INFO, "☁️ Generated auth token for cloud tunnel");
  }

  bool shouldTunnel = enabled;
  const bool tunnelAll =
      espWifi_->config["cloudTunnel"]["tunnelAll"].isNull()
          ? false
          : espWifi_->config["cloudTunnel"]["tunnelAll"].as<bool>();
  const bool allowSecondary =
      espWifi_->config["cloudTunnel"]["allowSecondary"].isNull()
          ? false
          : espWifi_->config["cloudTunnel"]["allowSecondary"].as<bool>();
  if (shouldTunnel && !tunnelAll) {
    // If uris array exists and is non-empty, require uri_ to be in it.
    if (espWifi_->config["cloudTunnel"]["uris"].is<JsonArray>()) {
      JsonArray arr = espWifi_->config["cloudTunnel"]["uris"].as<JsonArray>();
      if (arr.size() > 0) {
        bool found = false;
        for (JsonVariant v : arr) {
          const char *u = v.as<const char *>();
          if (u != nullptr && uri_[0] != '\0' && strcmp(u, uri_) == 0) {
            found = true;
            break;
          }
        }
        shouldTunnel = found;
      }
    }
  }

  // Stability guard: by default, only tunnel the control channel.
  // Multiple simultaneous TLS websocket clients can exceed internal heap on
  // some builds and cause MBEDTLS_ERR_SSL_ALLOC_FAILED (-0x7F00).
  if (shouldTunnel && strcmp(uri_, "/ws/control") != 0 && !allowSecondary) {
    shouldTunnel = false;
  }

  // Resource guard: camera tunneling is expensive (TLS + websocket client).
  // If the camera isn't enabled, don't keep an always-on tunnel for it.
  // This keeps heap pressure lower and avoids TLS allocation failures when
  // multiple tunnels are enabled.
  if (shouldTunnel && strcmp(uri_, "/ws/camera") == 0) {
    const bool camEnabled =
        espWifi_->config["camera"]["enabled"].isNull()
            ? false
            : espWifi_->config["camera"]["enabled"].as<bool>();
    if (!camEnabled) {
      shouldTunnel = false;
    }
  }

  configureCloudTunnel(baseUrl ? baseUrl : "", /*deviceId*/ nullptr,
                       token.c_str(), /*tunnelKey*/ nullptr);
  setCloudTunnelEnabled(shouldTunnel && cloudBaseUrl_[0] != '\0');
}

void WebSocket::cloudEventHandler_(void *handler_args, esp_event_base_t base,
                                   int32_t event_id, void *event_data) {
  (void)base;
  WebSocket *ws = static_cast<WebSocket *>(handler_args);
  if (ws == nullptr) {
    return;
  }

  if (event_id == WEBSOCKET_EVENT_CONNECTED) {
    ws->cloudTunnelConnected_ = true;
    ws->cloudUIConnected_ = false; // broker UI not yet attached
    if (ws->espWifi_) {
      ws->espWifi_->log(INFO, "☁️ WS cloud tunnel connected: %s", ws->uri_);
    }
    if (ws->onConnect_) {
      ws->onConnect_(ws, WebSocket::kCloudClientFd, ws->espWifi_);
    }
    return;
  }

  if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
    ws->cloudTunnelConnected_ = false;
    ws->cloudUIConnected_ = false;
    if (ws->espWifi_) {
      ws->espWifi_->log(INFO, "☁️ WS cloud tunnel disconnected: %s", ws->uri_);
    }
    if (ws->onDisconnect_) {
      ws->onDisconnect_(ws, WebSocket::kCloudClientFd, ws->espWifi_);
    }
    return;
  }

  if (event_id == WEBSOCKET_EVENT_DATA) {
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;
    if (d == nullptr) {
      return;
    }
    // Only accept whole frames (no fragmentation) to keep RAM bounded.
    if (d->payload_offset != 0 || d->payload_len != d->data_len) {
      if (ws->espWifi_) {
        ws->espWifi_->log(
            WARNING, "☁️ WS cloud rx fragmented; dropping (uri=%s)", ws->uri_);
      }
      return;
    }
    if ((size_t)d->data_len > ws->maxMessageLen_) {
      if (ws->espWifi_) {
        ws->espWifi_->log(WARNING, "☁️ WS cloud rx too large (%d) (uri=%s)",
                          (int)d->data_len, ws->uri_);
      }
      return;
    }

    httpd_ws_type_t t = HTTPD_WS_TYPE_BINARY;
    if (d->op_code == 0x1 /*TEXT*/) {
      t = HTTPD_WS_TYPE_TEXT;
    } else if (d->op_code == 0x2 /*BINARY*/) {
      t = HTTPD_WS_TYPE_BINARY;
    } else if (d->op_code == 0x8 /*CLOSE*/) {
      t = HTTPD_WS_TYPE_CLOSE;
    } else {
      // Ignore ping/pong/continuation at this layer.
      return;
    }

    // Intercept broker control messages ("registered", "ui_connected", ...).
    // IMPORTANT: avoid large stack buffers here; this runs on
    // esp_websocket_client's internal websocket_task stack.
    if (t == HTTPD_WS_TYPE_TEXT && d->data_ptr != nullptr && d->data_len > 0) {
      char typeC[32] = {0};
      if (jsonGetStringField((const char *)d->data_ptr, (size_t)d->data_len,
                             "type", typeC, sizeof(typeC))) {
        bool handledBrokerMeta = false;
        if (strcmp(typeC, "registered") == 0) {
          char ui[sizeof(ws->cloudUIWSURL_)] = {0};
          char dev[sizeof(ws->cloudDeviceWSURL_)] = {0};
          (void)jsonGetStringField((const char *)d->data_ptr,
                                   (size_t)d->data_len, "ui_ws_url", ui,
                                   sizeof(ui));
          (void)jsonGetStringField((const char *)d->data_ptr,
                                   (size_t)d->data_len, "device_ws_url", dev,
                                   sizeof(dev));
          if (ui[0]) {
            safeCopy(ws->cloudUIWSURL_, sizeof(ws->cloudUIWSURL_), ui);
          }
          if (dev[0]) {
            safeCopy(ws->cloudDeviceWSURL_, sizeof(ws->cloudDeviceWSURL_), dev);
          }
          ws->cloudRegisteredAtMs_ = (uint32_t)millis();
          if (ws->espWifi_) {
            ws->espWifi_->log(INFO, "☁️ WS tunnel registered: %s ui=%s",
                              ws->uri_,
                              (ws->cloudUIWSURL_[0] ? ws->cloudUIWSURL_ : "—"));
          }
          handledBrokerMeta = true;
        } else if (strcmp(typeC, "ui_connected") == 0) {
          ws->cloudUIConnected_ = true;
          if (ws->espWifi_) {
            ws->espWifi_->log(INFO, "☁️ UI attached to tunnel: %s", ws->uri_);
          }
          handledBrokerMeta = true;
        } else if (strcmp(typeC, "ui_disconnected") == 0) {
          ws->cloudUIConnected_ = false;
          if (ws->espWifi_) {
            ws->espWifi_->log(INFO, "☁️ UI detached from tunnel: %s", ws->uri_);
          }
          handledBrokerMeta = true;
        }

        // Don't feed broker meta-messages into endpoint handlers (especially
        // /ws/control). Those handlers should only see frames from the UI.
        if (handledBrokerMeta) {
          return;
        }
      }
    }

    if (ws->onMessage_) {
      ws->onMessage_(ws, WebSocket::kCloudClientFd, t,
                     (const uint8_t *)d->data_ptr, (size_t)d->data_len,
                     ws->espWifi_);
    }
    return;
  }
}

void WebSocket::startCloudTunnelTask_() {
  if (cloudTask_ != nullptr) {
    return;
  }
  if (cloudMutex_ == nullptr) {
    cloudMutex_ = (void *)xSemaphoreCreateMutex();
  }
  // Create a small background task that keeps the cloud tunnel connected.
  TaskHandle_t th = nullptr;
  // Note: this task builds URLs and calls into esp_websocket_client_init/start,
  // which can use non-trivial stack. If this overflows, it can corrupt heap and
  // cause spurious FreeRTOS asserts (e.g. xQueueSemaphoreTake).
  const BaseType_t ok = xTaskCreate(&WebSocket::cloudTaskTrampoline_, "wsCloud",
                                    8192, this, 5, &th);
  if (ok == pdPASS) {
    cloudTask_ = (void *)th;
  } else if (espWifi_) {
    espWifi_->log(WARNING, "☁️ Failed to start WS cloud task: %s", uri_);
  }
}

void WebSocket::stopCloudTunnel_() {
  cloudTunnelConnected_ = false;

  SemaphoreHandle_t mu = (SemaphoreHandle_t)cloudMutex_;
  const bool locked =
      (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(200)) == pdTRUE);

  esp_websocket_client_handle_t c = (esp_websocket_client_handle_t)cloudClient_;
  cloudClient_ = nullptr;
  if (c) {
    (void)esp_websocket_client_stop(c);
    esp_websocket_client_destroy(c);
  }

  if (locked) {
    (void)xSemaphoreGive(mu);
  }
}

void WebSocket::cloudTaskTrampoline_(void *arg) {
  WebSocket *ws = static_cast<WebSocket *>(arg);
  if (ws == nullptr) {
    vTaskDelete(nullptr);
    return;
  }

  int backoffMs = 1000;
  for (;;) {
    if (!ws->cloudTunnelEnabled_) {
      break;
    }
    if (ws->espWifi_ == nullptr || !ws->espWifi_->isWiFiConnected()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (ws->cloudBaseUrl_[0] == '\0' || ws->cloudDeviceId_[0] == '\0') {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // Build ws(s) URL:
    // <base>/ws/device/<deviceId>?announce=1&tunnel=<tunnelKey>&token=<token>&claim=<claim>
    char encTunnel[WebSocket::kMaxCloudTunnelKeyLen * 3] = {0};
    char encToken[WebSocket::kMaxCloudTokenLen * 3] = {0};
    char encClaim[32] = {0};
    urlEncode(ws->cloudTunnelKey_, encTunnel, sizeof(encTunnel));
    urlEncode(ws->cloudToken_, encToken, sizeof(encToken));
    const std::string claim =
        (ws->espWifi_ != nullptr) ? ws->espWifi_->getClaimCode(false) : "";
    if (!claim.empty()) {
      urlEncode(claim.c_str(), encClaim, sizeof(encClaim));
    }

    char url[768] = {0};
    const bool hasTunnel = (encTunnel[0] != '\0');
    const bool hasToken = (encToken[0] != '\0');
    safeAppend(url, sizeof(url), ws->cloudBaseUrl_);
    safeAppend(url, sizeof(url), "/ws/device/");
    safeAppend(url, sizeof(url), ws->cloudDeviceId_);
    safeAppend(url, sizeof(url), "?announce=1");
    if (hasTunnel) {
      safeAppend(url, sizeof(url), "&tunnel=");
      safeAppend(url, sizeof(url), encTunnel);
    }
    if (hasToken) {
      safeAppend(url, sizeof(url), "&token=");
      safeAppend(url, sizeof(url), encToken);
    }
    if (encClaim[0] != '\0') {
      safeAppend(url, sizeof(url), "&claim=");
      safeAppend(url, sizeof(url), encClaim);
    }
    // If truncated, log and skip this attempt (prevents connecting to a broken
    // URL).
    if (url[sizeof(url) - 1] != '\0') {
      if (ws->espWifi_) {
        ws->espWifi_->log(WARNING, "☁️ WS cloud URL too long; skipping (uri=%s)",
                          ws->uri_);
      }
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    esp_websocket_client_config_t cfg = {};
    cfg.uri = url;
    // Cloud broker → device traffic is tiny (control JSON + broker status).
    // Using the WS endpoint's broadcast size here is dangerous: `/ws/camera`
    // sets maxBroadcastLen_ to 128KB, which can cause large allocations in
    // esp_websocket_client and lead to TLS alloc failures / task creation
    // failures over time.
    //
    // We cap the websocket client buffer aggressively. TX of large binary
    // frames (camera) does not require a giant RX buffer.
    // Buffer sizing: use the larger of maxMessageLen_ and a capped
    // maxBroadcastLen_ to keep control payloads (e.g. logs/config) stable over
    // the tunnel, without allowing camera-sized buffers.
    int buf = (int)ws->maxMessageLen_;
    int cappedBroadcast = (int)ws->maxBroadcastLen_;
    if (cappedBroadcast > 8192) {
      cappedBroadcast = 8192;
    }
    if (cappedBroadcast > buf) {
      buf = cappedBroadcast;
    }
    if (buf < 1024) {
      buf = 1024;
    }
    if (buf > 8192) {
      buf = 8192;
    }
    cfg.buffer_size = buf;
    // esp_websocket_client creates its own internal websocket_task.
    // Use smaller stacks for non-camera tunnels to preserve heap for TLS.
    // 4096 is too small for esp_websocket_client's websocket_task on this
    // build (it overflows immediately after connect). Use a safer default.
    cfg.task_stack = (strcmp(ws->uri_, "/ws/camera") == 0) ? 6144 : 6144;
    cfg.task_prio = 5;
    cfg.network_timeout_ms = 15000;
    cfg.disable_auto_reconnect = true;

#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    // Enable TLS server verification for wss:// using the ESP-IDF cert bundle.
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    // Serialize dial attempts across endpoints to reduce transient TLS heap.
    SemaphoreHandle_t dial = cloudDialMu();
    const bool dialLocked =
        (dial != nullptr) &&
        (xSemaphoreTake(dial, pdMS_TO_TICKS(15000)) == pdTRUE);

    SemaphoreHandle_t mu = (SemaphoreHandle_t)ws->cloudMutex_;
    const bool locked =
        (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(200)) == pdTRUE);
    esp_websocket_client_handle_t c = esp_websocket_client_init(&cfg);
    if (c) {
      ws->cloudClient_ = (void *)c;
      (void)esp_websocket_register_events(c, WEBSOCKET_EVENT_ANY,
                                          &WebSocket::cloudEventHandler_, ws);
      (void)esp_websocket_client_start(c);
    }
    if (locked) {
      (void)xSemaphoreGive(mu);
    }

    // Hold the dial mutex briefly while TLS setup is most memory-hungry.
    if (dialLocked) {
      const int64_t holdStartUs = esp_timer_get_time();
      while (ws->cloudTunnelEnabled_ && !ws->cloudTunnelConnected_) {
        const int64_t heldMs = (esp_timer_get_time() - holdStartUs) / 1000;
        if (heldMs > 2000) {
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      (void)xSemaphoreGive(dial);
    }

    // Give the handshake some time to complete before checking connected
    // state; otherwise we can race and flap.
    const int64_t connectStartUs = esp_timer_get_time();
    while (ws->cloudTunnelEnabled_ && !ws->cloudTunnelConnected_) {
      const int64_t waitedMs = (esp_timer_get_time() - connectStartUs) / 1000;
      if (waitedMs > 15000) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Wait while connected/enabled; reconnect on drop.
    const int64_t startUs = esp_timer_get_time();
    while (ws->cloudTunnelEnabled_) {
      // Prefer event-driven connected flag; it's stable across internals.
      if (!ws->cloudTunnelConnected_) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(250));
    }

    // Stop/destroy the current client, but keep enabled=true so we can
    // reconnect after backoff.
    ws->cloudTunnelConnected_ = false;
    SemaphoreHandle_t mu3 = (SemaphoreHandle_t)ws->cloudMutex_;
    const bool locked3 =
        (mu3 != nullptr) && (xSemaphoreTake(mu3, pdMS_TO_TICKS(200)) == pdTRUE);
    esp_websocket_client_handle_t c3 =
        (esp_websocket_client_handle_t)ws->cloudClient_;
    ws->cloudClient_ = nullptr;
    if (c3) {
      (void)esp_websocket_client_stop(c3);
      esp_websocket_client_destroy(c3);
    }
    if (locked3) {
      (void)xSemaphoreGive(mu3);
    }

    // Backoff with cap; reset quickly if we stayed connected for a while.
    const int64_t durMs = (esp_timer_get_time() - startUs) / 1000;
    if (durMs > 30000) {
      backoffMs = 1000;
    } else if (backoffMs < 30000) {
      backoffMs *= 2;
      if (backoffMs > 30000) {
        backoffMs = 30000;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(backoffMs));
  }

  ws->cloudTask_ = nullptr;
  vTaskDelete(nullptr);
}

esp_err_t WebSocket::textAll(const char *message) {
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return textAll(message, strlen(message));
}

esp_err_t WebSocket::textAll(const char *message, size_t len) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)message;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || espWifi_ == nullptr || espWifi_->webServer == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    return ESP_OK;
  }
  // Bounded allocation: refuse absurdly large broadcasts.
  if (len > maxBroadcastLen_) {
    return ESP_ERR_INVALID_SIZE;
  }

  BroadcastJob *job = (BroadcastJob *)malloc(sizeof(BroadcastJob) + len);
  if (job == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  job->ws = this;
  job->type = HTTPD_WS_TYPE_TEXT;
  job->len = len;
  memcpy((uint8_t *)(job + 1), message, len);

  return httpd_queue_work(espWifi_->webServer,
                          &WebSocket::broadcastWorkTrampoline, job);
#endif
}

esp_err_t WebSocket::textTo(int clientFd, const char *message) {
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return textTo(clientFd, message, strlen(message));
}

esp_err_t WebSocket::textTo(int clientFd, const char *message, size_t len) {
  if (message == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    return ESP_OK;
  }
  if (len > maxBroadcastLen_) {
    return ESP_ERR_INVALID_SIZE;
  }

  // Cloud synthetic "fd"
  if (clientFd == kCloudClientFd) {
    if (cloudTunnelEnabled_ && cloudTunnelConnected_) {
      return cloudSendFrame_(HTTPD_WS_TYPE_TEXT, (const uint8_t *)message, len);
    }
    return ESP_ERR_INVALID_STATE;
  }

#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)clientFd;
  (void)message;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || espWifi_ == nullptr || espWifi_->webServer == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = (uint8_t *)message;
  frame.len = len;

  return httpd_ws_send_frame_async(espWifi_->webServer, clientFd, &frame);
#endif
}

esp_err_t WebSocket::binaryAll(const uint8_t *data, size_t len) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)data;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || espWifi_ == nullptr || espWifi_->webServer == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (data == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    return ESP_OK;
  }
  if (len > maxBroadcastLen_) {
    return ESP_ERR_INVALID_SIZE;
  }

  BroadcastJob *job = (BroadcastJob *)malloc(sizeof(BroadcastJob) + len);
  if (job == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  job->ws = this;
  job->type = HTTPD_WS_TYPE_BINARY;
  job->len = len;
  memcpy((uint8_t *)(job + 1), data, len);

  return httpd_queue_work(espWifi_->webServer,
                          &WebSocket::broadcastWorkTrampoline, job);
#endif
}

void WebSocket::closeAll() {
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (!started_ || espWifi_ == nullptr || espWifi_->webServer == nullptr) {
    return;
  }
  httpd_handle_t hd = espWifi_->webServer;
  // Best-effort close; keep bounded.
  for (size_t i = 0; i < clientCount_;) {
    int fd = clientFds_[i];
    (void)httpd_sess_trigger_close(hd, fd);
    removeClient_(fd);
  }
#endif
}
