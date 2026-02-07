#include <WebSocket.h>

#include <cstring>
#include <memory>
#include <new>

/**
 * @brief HTTP server trampoline that routes requests to a WebSocket instance.
 *
 * ESP-IDF expects a C-style handler signature. We store `this` in `user_ctx`
 * and forward the request to `handleWsRequest()`.
 *
 * @param req Incoming HTTPD request (WebSocket handshake or data frame).
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG on bad inputs.
 */
esp_err_t WebSocket::wsHandlerTrampoline(httpd_req_t *req) {
  if (req == nullptr || req->user_ctx == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return static_cast<WebSocket *>(req->user_ctx)->handleWsRequest(req);
}

/**
 * @brief Track a connected client socket FD for this endpoint.
 *
 * Keeps a bounded list of active client FDs. If at capacity, the oldest client
 * FD is dropped from tracking to make room (actual socket close is handled
 * elsewhere).
 *
 * @param fd Socket FD for the connected client.
 */
void WebSocket::addClient_(int fd) {
  if (fd < 0) {
    return;
  }
  for (size_t i = 0; i < clientCount_; ++i) {
    if (clientFds_[i] == fd) {
      return;
    }
  }
  if (clientCount_ >= maxClients_) {
    if (maxClients_ > 1) {
      for (size_t i = 1; i < maxClients_; ++i) {
        clientFds_[i - 1] = clientFds_[i];
      }
      clientFds_[maxClients_ - 1] = fd;
    } else {
      clientFds_[0] = fd;
    }
    return;
  }
  clientFds_[clientCount_++] = fd;
}

/**
 * @brief Remove a client socket FD from tracking.
 *
 * @param fd Socket FD to remove.
 */
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

/**
 * @brief Handle a WebSocket request (handshake GET or a WS frame).
 *
 * - On HTTP_GET: performs optional auth, enforces per-endpoint client limits,
 *   registers the client, and calls `onConnect_`.
 * - On WS frames: receives metadata, optionally receives payload, invokes
 *   `onMessage_`, and handles CLOSE frames by removing the client and calling
 *   `onDisconnect_`.
 *
 * @param req HTTPD request.
 * @return esp_err_t ESP_OK on success, or an ESP-IDF error code.
 */
esp_err_t WebSocket::handleWsRequest(httpd_req_t *req) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)req;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (req == nullptr || server_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  // Handshake (HTTP GET)
  if (req->method == HTTP_GET) {
    const int fd = httpd_req_to_sockfd(req);

    if (requireAuth_ && authCheck_ != nullptr) {
      if (!authCheck_(req, userCtx_)) {
        // Best-effort send a proper Close frame so clients don't see 1006.
        constexpr uint16_t kCloseCode = 1008; // Policy Violation
        // Keep as an array so sizeof() is compile-time safe.
        constexpr char kReason[] = "unauthorized";

        // Close payload = 2-byte code (network-order) + UTF-8 reason.
        uint8_t payload[2 + 32];
        size_t n = 0;
        payload[n++] = (uint8_t)((kCloseCode >> 8) & 0xff);
        payload[n++] = (uint8_t)(kCloseCode & 0xff);

        // Avoid strnlen() on a literal with a larger bound; some toolchains
        // warn about possible over-read (-Wstringop-overread).
        constexpr size_t kReasonLen = sizeof(kReason) - 1; // exclude NUL
        memcpy(payload + n, kReason, kReasonLen);
        n += kReasonLen;

        httpd_ws_frame_t closeFrame;
        memset(&closeFrame, 0, sizeof(closeFrame));
        closeFrame.type = HTTPD_WS_TYPE_CLOSE;
        closeFrame.payload = payload;
        closeFrame.len = n;
        (void)httpd_ws_send_frame_async(server_, fd, &closeFrame);

        (void)httpd_sess_trigger_close(server_, fd);
        return ESP_OK;
      }
    }

    // Enforce single-client endpoints by dropping the existing connection.
    if (maxClients_ == 1 && clientCount_ >= 1) {
      (void)httpd_sess_trigger_close(server_, clientFds_[0]);
      clientCount_ = 0;
    }

    addClient_(fd);

    if (onConnect_) {
      onConnect_(this, fd, userCtx_);
    }
    return ESP_OK;
  }

  // Data frame
  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));

  esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  const int fd = httpd_req_to_sockfd(req);

  if (frame.type == HTTPD_WS_TYPE_CLOSE) {
    removeClient_(fd);
    if (onDisconnect_) {
      onDisconnect_(this, fd, userCtx_);
    }
    return ESP_OK;
  }

  if (frame.len > maxMessageLen_) {
    return ESP_FAIL;
  }

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
               frame.len, userCtx_);
  }

  return ESP_OK;
#endif
}

/**
 * @brief Destructor. Unregisters the URI handler when applicable.
 */
WebSocket::~WebSocket() { end(); }

/**
 * @brief Unregister the WebSocket URI and clear state. Call before stopping
 * the HTTP server so the handle remains valid for unregister.
 */
void WebSocket::end() {
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (started_ && server_ != nullptr && uri_[0] != '\0') {
    closeAll();
    (void)httpd_unregister_uri_handler(server_, uri_, HTTP_GET);
  }
  started_ = false;
  server_ = nullptr;
  uri_[0] = '\0';
#endif
}

/**
 * @brief Register a WebSocket endpoint on the ESP-IDF HTTP server.
 *
 * @param uri URI path for this WS endpoint (e.g. "/ws/control").
 * @param server HTTPD server handle.
 * @param userCtx Opaque context pointer passed back to callbacks.
 * @param onMessage Called for each received WS message frame.
 * @param onConnect Called after a client successfully connects.
 * @param onDisconnect Called when a client disconnects/closes.
 * @param maxMessageLen Max inbound frame payload bytes accepted (clamped).
 * @param maxBroadcastLen Max outbound frame payload bytes accepted (clamped).
 * @param requireAuth Whether to call `authCheck` during handshake.
 * @param authCheck Auth callback used when `requireAuth` is true.
 * @param maxClients Per-endpoint client cap (clamped to internal max).
 * @return true on success; false if registration fails or WS support is off.
 */
bool WebSocket::begin(const char *uri, httpd_handle_t server, void *userCtx,
                      OnMessageCb onMessage, OnConnectCb onConnect,
                      OnDisconnectCb onDisconnect, size_t maxMessageLen,
                      size_t maxBroadcastLen, bool requireAuth,
                      AuthCheckFn authCheck, size_t maxClients) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)uri;
  (void)server;
  (void)userCtx;
  (void)onMessage;
  (void)onConnect;
  (void)onDisconnect;
  (void)maxMessageLen;
  (void)maxBroadcastLen;
  (void)requireAuth;
  (void)authCheck;
  (void)maxClients;
  return false;
#else
  if (uri == nullptr || server == nullptr) {
    return false;
  }

  server_ = server;
  userCtx_ = userCtx;
  onMessage_ = onMessage;
  onConnect_ = onConnect;
  onDisconnect_ = onDisconnect;
  requireAuth_ = requireAuth;
  authCheck_ = authCheck;

  maxMessageLen_ = (maxMessageLen == 0) ? 1 : maxMessageLen;
  if (maxMessageLen_ > 8192) {
    maxMessageLen_ = 8192;
  }
  maxBroadcastLen_ = (maxBroadcastLen == 0) ? 1 : maxBroadcastLen;
  if (maxBroadcastLen_ > 262144) {
    maxBroadcastLen_ = 262144;
  }
  // Set per-endpoint client limit (clamped to kMaxClients)
  maxClients_ = (maxClients == 0) ? 1 : maxClients;
  if (maxClients_ > kMaxClients) {
    maxClients_ = kMaxClients;
  }

  uri_[0] = '\0';
  (void)snprintf(uri_, sizeof(uri_), "%s", uri);

  httpd_uri_t wsUri = {
      .uri = uri_,
      .method = HTTP_GET,
      .handler = &WebSocket::wsHandlerTrampoline,
      .user_ctx = this,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = nullptr,
  };

  esp_err_t err = httpd_register_uri_handler(server_, &wsUri);
  if (err != ESP_OK) {
    started_ = false;
    return false;
  }

  started_ = true;
  return true;
#endif
}

/**
 * @brief Send a UTF-8 text frame to a single client.
 *
 * @param clientFd Target client socket FD.
 * @param message NUL-terminated message text.
 * @return esp_err_t ESP_OK on success; otherwise an ESP-IDF error code.
 */
esp_err_t WebSocket::sendText(int clientFd, const char *message) {
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return sendText(clientFd, message, strlen(message));
}

/**
 * @brief Send a text frame to a single client.
 *
 * @param clientFd Target client socket FD.
 * @param message Message bytes (may be non-NUL; must be valid for len).
 * @param len Number of bytes to send.
 * @return esp_err_t ESP_OK on success; otherwise an ESP-IDF error code.
 */
esp_err_t WebSocket::sendText(int clientFd, const char *message, size_t len) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)clientFd;
  (void)message;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || server_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (message == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    return ESP_OK;
  }
  if (len > maxBroadcastLen_) {
    return ESP_ERR_INVALID_SIZE;
  }

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = (uint8_t *)message;
  frame.len = len;

  esp_err_t err = httpd_ws_send_frame_async(server_, clientFd, &frame);
  if (err != ESP_OK) {
    removeClient_(clientFd);
    if (onDisconnect_) {
      onDisconnect_(this, clientFd, userCtx_);
    }
  }
  return err;
#endif
}

/**
 * @brief Send a binary frame to a single client.
 *
 * @param clientFd Target client socket FD.
 * @param data Payload bytes.
 * @param len Payload size in bytes.
 * @return esp_err_t ESP_OK on success; otherwise an ESP-IDF error code.
 */
esp_err_t WebSocket::sendBinary(int clientFd, const uint8_t *data, size_t len) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)clientFd;
  (void)data;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || server_ == nullptr) {
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

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = HTTPD_WS_TYPE_BINARY;
  frame.payload = (uint8_t *)data;
  frame.len = len;

  esp_err_t err = httpd_ws_send_frame_async(server_, clientFd, &frame);
  if (err != ESP_OK) {
    removeClient_(clientFd);
    if (onDisconnect_) {
      onDisconnect_(this, clientFd, userCtx_);
    }
  }
  return err;
#endif
}

/**
 * @brief Broadcast a UTF-8 text message to all tracked clients.
 *
 * @param message NUL-terminated message text.
 * @return esp_err_t ESP_OK on success; otherwise an ESP-IDF error code.
 */
esp_err_t WebSocket::broadcastText(const char *message) {
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return broadcastText(message, strlen(message));
}

/**
 * @brief Broadcast a text frame to all tracked clients.
 *
 * Clients that fail to receive the frame are removed from tracking and trigger
 * `onDisconnect_`.
 *
 * @param message Message bytes.
 * @param len Number of bytes to send.
 * @return esp_err_t ESP_OK on success; otherwise an ESP-IDF error code.
 */
esp_err_t WebSocket::broadcastText(const char *message, size_t len) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)message;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || server_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    return ESP_OK;
  }
  if (len > maxBroadcastLen_) {
    return ESP_ERR_INVALID_SIZE;
  }

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = (uint8_t *)message;
  frame.len = len;

  for (size_t i = 0; i < clientCount_;) {
    int fd = clientFds_[i];
    esp_err_t err = httpd_ws_send_frame_async(server_, fd, &frame);
    if (err != ESP_OK) {
      if (onDisconnect_) {
        onDisconnect_(this, fd, userCtx_);
      }
      removeClient_(fd);
      continue;
    }
    ++i;
  }

  return ESP_OK;
#endif
}

/**
 * @brief Broadcast a binary frame to all tracked clients.
 *
 * @param data Payload bytes.
 * @param len Payload size in bytes.
 * @return esp_err_t ESP_OK on success; otherwise an ESP-IDF error code.
 */
esp_err_t WebSocket::broadcastBinary(const uint8_t *data, size_t len) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)data;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || server_ == nullptr) {
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

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = HTTPD_WS_TYPE_BINARY;
  frame.payload = (uint8_t *)data;
  frame.len = len;

  // Send to all clients
  for (size_t i = 0; i < clientCount_;) {
    int fd = clientFds_[i];
    esp_err_t err = httpd_ws_send_frame_async(server_, fd, &frame);
    if (err != ESP_OK) {
      if (onDisconnect_) {
        onDisconnect_(this, fd, userCtx_);
      }
      removeClient_(fd);
      continue;
    }
    ++i;
  }

  return ESP_OK;
#endif
}

/**
 * @brief Close all tracked client sessions for this endpoint.
 *
 * Uses `httpd_sess_trigger_close()` and removes clients from internal tracking.
 */
void WebSocket::closeAll() {
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (!started_ || server_ == nullptr) {
    return;
  }
  for (size_t i = 0; i < clientCount_;) {
    int fd = clientFds_[i];
    (void)httpd_sess_trigger_close(server_, fd);
    removeClient_(fd);
  }
#endif
}
