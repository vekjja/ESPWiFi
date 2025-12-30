#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT
// Best-effort client IP/port logging (no heap allocations)
#include "lwip/inet.h"
#include "lwip/sockets.h"
#else
// When CONFIG_HTTPD_WS_SUPPORT is disabled, esp_http_server.h does not declare
// websocket types. Provide tiny fallbacks so this header stays buildable.
typedef int httpd_ws_type_t;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_TEXT = 0;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_BINARY = 1;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_CLOSE = 2;
#endif

class WebSocket {
private:
  // Keep this wrapper IDF-friendly: no exceptions, bounded memory,
  // and avoid heavy per-message allocations.
  ESPWiFi *espWifi_ = nullptr;

  // We copy the URI into a fixed buffer so the caller doesn't need to keep the
  // original string alive.
  static constexpr size_t kMaxUriLen = 64;
  char uri_[kMaxUriLen] = {0};

  // Track clients that connected to *this* WS endpoint (not all WS clients).
  // IDF's httpd WS API doesn't expose URI->fd mapping, so we keep a small list.
  static constexpr size_t kMaxClients = 8; // matches/overlaps our httpd config
  int clientFds_[kMaxClients] = {0};
  size_t clientCount_ = 0;

  size_t maxMessageLen_ = 1024;
  size_t maxBroadcastLen_ = 8192;
  std::vector<uint8_t> rxBuf_;

public:
  // Callbacks kept as plain function pointers for low overhead.
  using OnConnectCb = void (*)(WebSocket *ws, int clientFd, ESPWiFi *espWifi);
  using OnDisconnectCb = void (*)(WebSocket *ws, int clientFd,
                                  ESPWiFi *espWifi);
  using OnMessageCb = void (*)(WebSocket *ws, int clientFd,
                               httpd_ws_type_t type, const uint8_t *data,
                               size_t len, ESPWiFi *espWifi);

private:
  OnConnectCb onConnect_ = nullptr;
  OnDisconnectCb onDisconnect_ = nullptr;
  OnMessageCb onMessage_ = nullptr;

  bool started_ = false;
  bool requireAuth_ = false;

#ifdef CONFIG_HTTPD_WS_SUPPORT
  static void getRemoteInfo_(int fd, char *outIp, size_t outIpLen,
                             uint16_t *outPort) {
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
  }
#endif

  static esp_err_t wsHandlerTrampoline(httpd_req_t *req) {
    if (req == nullptr || req->user_ctx == nullptr) {
      return ESP_ERR_INVALID_ARG;
    }
    return static_cast<WebSocket *>(req->user_ctx)->handleWsRequest(req);
  }

  void addClient_(int fd) {
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

  void removeClient_(int fd) {
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

  esp_err_t handleWsRequest(httpd_req_t *req) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
    (void)req;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (espWifi_ == nullptr || espWifi_->webServer == nullptr ||
        req == nullptr) {
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
      if (requireAuth_ && espWifi_ != nullptr && espWifi_->authEnabled() &&
          !espWifi_->isExcludedPath(req->uri)) {
        bool ok = espWifi_->authorized(req);
        if (!ok) {
          // Browser WebSocket APIs can't set Authorization headers. Allow token
          // via query param: ws://host/path?token=...
          std::string tok = espWifi_->getQueryParam(req, "token");
          std::string expected =
              espWifi_->config["auth"]["token"].as<std::string>();
          ok = (!tok.empty() && !expected.empty() && tok == expected);
        }
        if (!ok) {
          if (espWifi_) {
            espWifi_->log(WARNING, "🔒 WS(%s) unauthorized; closing (fd=%d)",
                          uri_, fd);
          }
          // Best-effort: terminate the session.
          (void)httpd_sess_trigger_close(espWifi_->webServer, fd);
          return ESP_OK;
        }
      }

      addClient_(fd);
      if (espWifi_) {
        char ip[64];
        uint16_t port = 0;
        getRemoteInfo_(fd, ip, sizeof(ip), &port);
        if (ip[0] != '\0') {
          espWifi_->log(INFO, "🕸️🔗 WebSocket Client Connected: %s", uri_);
          espWifi_->log(DEBUG, "\tFD: %d", fd);
          espWifi_->log(DEBUG, "\tIP: %s", ip);
          espWifi_->log(DEBUG, "\tPort: %u", (unsigned)port);
        } else {
          espWifi_->log(INFO, "🕸️🔗 WebSocket Client Connected: %s (fd=%d)",
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
      if (espWifi_) {
        espWifi_->log(
            INFO,
            "🕸️ WebSocket Client Disconnected: %s (fd=%d) ⛓️‍💥",
            uri_, fd);
        espWifi_->log(DEBUG, "\tDisconnect Time: %llu ms",
                      (unsigned long long)(esp_timer_get_time() / 1000ULL));
      }
      removeClient_(fd);
      if (onDisconnect_) {
        onDisconnect_(this, fd, espWifi_);
      }
      return ESP_OK;
    }

    if (frame.len > maxMessageLen_) {
      // Too large to buffer safely (bounded RAM). Returning failure will cause
      // the server to close/cleanup the session.
      if (espWifi_) {
        espWifi_->log(WARNING, "WS(%s) rx too large: %u > %u", uri_,
                      (unsigned)frame.len, (unsigned)maxMessageLen_);
      }
      return ESP_FAIL;
    }

    if (frame.len > 0) {
      if (rxBuf_.size() < maxMessageLen_) {
        rxBuf_.resize(maxMessageLen_);
      }
      frame.payload = rxBuf_.data();
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

  struct BroadcastJob {
    WebSocket *ws;
    httpd_ws_type_t type;
    size_t len;
    // payload bytes follow
  };

  static void broadcastWorkTrampoline(void *arg) {
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

  void broadcastNow_(httpd_ws_type_t type, const uint8_t *payload, size_t len) {
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
        if (espWifi_) {
          espWifi_->log(
              INFO,
              "🕸️ WebSocket Client Disconnected: %s (fd=%d) ⛓️‍💥",
              uri_, fd);
        }
        if (onDisconnect_) {
          onDisconnect_(this, fd, espWifi_);
        }
        removeClient_(fd);
        continue; // don't increment i; removeClient_ shifts
      }
      ++i;
    }
#else
    (void)type;
    (void)payload;
    (void)len;
#endif
  }

public:
  WebSocket() = default;
  ~WebSocket() {
    if (started_ && espWifi_ != nullptr && espWifi_->webServer != nullptr &&
        uri_[0] != '\0') {
      // Best-effort cleanup; ignore errors (server may already be stopped).
      (void)httpd_unregister_uri_handler(espWifi_->webServer, uri_, HTTP_GET);
    }
  }

  // Initialize and register the WS endpoint.
  // Note: `uri` is copied into a fixed buffer (bounded RAM).
  bool begin(const char *uri, ESPWiFi *espWifi, OnMessageCb onMessage = nullptr,
             OnConnectCb onConnect = nullptr,
             OnDisconnectCb onDisconnect = nullptr, size_t maxMessageLen = 1024,
             size_t maxBroadcastLen = 8192, bool requireAuth = false) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
    (void)uri;
    (void)espWifi;
    (void)onMessage;
    (void)onConnect;
    (void)onDisconnect;
    (void)maxMessageLen;
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
    rxBuf_.clear();
    rxBuf_.shrink_to_fit();
    rxBuf_.resize(maxMessageLen_);

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
    return true;
#endif
  }

  operator bool() const { return started_; }

  size_t numClients() const { return clientCount_; }

  // Queue a broadcast into the HTTP server task for thread-safety and to keep
  // callers snappy (user-perceived performance).
  esp_err_t textAll(const char *message) {
    if (message == nullptr) {
      return ESP_ERR_INVALID_ARG;
    }
    return textAll(message, strlen(message));
  }

  esp_err_t textAll(const char *message, size_t len) {
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

  esp_err_t binaryAll(const uint8_t *data, size_t len) {
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

  void closeAll() {
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
};

#endif