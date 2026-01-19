#include <WebSocket.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <new>
#include <sys/socket.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ============================================================================
// Helper: Get remote peer info
// ============================================================================

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
    // Handle IPv4-mapped IPv6 addresses
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

// ============================================================================
// Request handler trampoline
// ============================================================================

esp_err_t WebSocket::wsHandlerTrampoline(httpd_req_t *req) {
  if (req == nullptr || req->user_ctx == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return static_cast<WebSocket *>(req->user_ctx)->handleWsRequest(req);
}

// ============================================================================
// Client management
// ============================================================================

void WebSocket::addClient_(int fd) {
  if (fd < 0) {
    return;
  }
  // Check if already tracked
  for (size_t i = 0; i < clientCount_; ++i) {
    if (clientFds_[i] == fd) {
      return; // already tracked
    }
  }
  // Check per-endpoint limit
  if (clientCount_ >= maxClients_) {
    // At limit: drop oldest client to make room (enforces maxClients_)
    if (maxClients_ > 1) {
      // Multi-client mode: shift array
      for (size_t i = 1; i < maxClients_; ++i) {
        clientFds_[i - 1] = clientFds_[i];
      }
      clientFds_[maxClients_ - 1] = fd;
    } else {
      // Single-client mode: replace (old client should be closed by handler)
      clientFds_[0] = fd;
    }
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

// ============================================================================
// WebSocket request handler
// ============================================================================

esp_err_t WebSocket::handleWsRequest(httpd_req_t *req) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)req;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (server_ == nullptr || req == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  // WebSocket handshake (HTTP GET)
  if (req->method == HTTP_GET) {
    const int fd = httpd_req_to_sockfd(req);

    // Optional auth check (happens before connection is accepted)
    if (requireAuth_ && authCheck_ != nullptr) {
      if (!authCheck_(req, userCtx_)) {
        // Auth failed.
        //
        // If we just drop the TCP session, most clients report code 1006
        // (abnormal closure). Instead, best-effort send a proper WebSocket
        // Close control frame so clients can show a meaningful error.
        //
        // Use 1008 (Policy Violation) with a short reason string.
        // Note: browsers don't surface this reason to JS for security, but
        // CLI tools (wscat) and logs will.
        constexpr uint16_t kCloseCode = 1008; // Policy Violation
        const char *reason = "unauthorized";
        const size_t reasonLen = (reason != nullptr) ? strlen(reason) : 0;

        // Close payload = 2-byte network-order code + UTF-8 reason.
        uint8_t payload[2 + 32];
        size_t n = 0;
        payload[n++] = (uint8_t)((kCloseCode >> 8) & 0xff);
        payload[n++] = (uint8_t)(kCloseCode & 0xff);

        // Bound reason length to keep stack usage fixed.
        const size_t copyLen = (reasonLen > 32) ? 32 : reasonLen;
        if (copyLen > 0 && reason != nullptr) {
          memcpy(payload + n, reason, copyLen);
          n += copyLen;
        }

        httpd_ws_frame_t closeFrame;
        memset(&closeFrame, 0, sizeof(closeFrame));
        closeFrame.type = HTTPD_WS_TYPE_CLOSE;
        closeFrame.payload = payload;
        closeFrame.len = n;
        (void)httpd_ws_send_frame_async(server_, fd, &closeFrame);

        // Close the session after sending the close frame (best-effort).
        (void)httpd_sess_trigger_close(server_, fd);
        return ESP_OK;
      }
    }

    // For single-client mode: close existing connection before adding new one
    // This allows specific endpoints to enforce single-client limits if needed
    if (maxClients_ == 1 && clientCount_ >= 1 && server_ != nullptr) {
      // Close existing client to enforce single-client limit
      (void)httpd_sess_trigger_close(server_, clientFds_[0]);
      clientCount_ = 0; // Reset count, new client will be added below
    }

    addClient_(fd);

    // Log connection
    char ip[64];
    uint16_t port = 0;
    getRemoteInfo_(fd, ip, sizeof(ip), &port);

    if (onConnect_) {
      onConnect_(this, fd, userCtx_);
    }
    return ESP_OK;
  }

  // WebSocket data frame
  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));

  // Get frame metadata
  esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  const int fd = httpd_req_to_sockfd(req);

  // Client closing
  if (frame.type == HTTPD_WS_TYPE_CLOSE) {
    removeClient_(fd);
    if (onDisconnect_) {
      onDisconnect_(this, fd, userCtx_);
    }
    return ESP_OK;
  }

  // Frame too large
  if (frame.len > maxMessageLen_) {
    return ESP_FAIL;
  }

  // Allocate buffer (stack for small, heap for large)
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

  // Read frame payload
  if (frame.len > 0) {
    frame.payload = buf;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
      return ret;
    }
  }

  // Invoke user callback
  if (onMessage_) {
    onMessage_(this, fd, frame.type,
               (frame.len > 0) ? (const uint8_t *)frame.payload : nullptr,
               frame.len, userCtx_);
  }

  return ESP_OK;
#endif
}

// ============================================================================
// Lifecycle
// ============================================================================

WebSocket::~WebSocket() {
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (started_ && server_ != nullptr && uri_[0] != '\0') {
    (void)httpd_unregister_uri_handler(server_, uri_, HTTP_GET);
  }
#endif
}

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

  // Keep limits sane and bounded
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

  // Copy URI
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

// ============================================================================
// Send operations
// ============================================================================

esp_err_t WebSocket::sendText(int clientFd, const char *message) {
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return sendText(clientFd, message, strlen(message));
}

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

  // Direct async send - no job queue, no memcpy
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

  // Direct async send - no job queue, no memcpy
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

esp_err_t WebSocket::broadcastText(const char *message) {
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return broadcastText(message, strlen(message));
}

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

  // Direct broadcast - no job queue
  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = (uint8_t *)message;
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

  // Direct broadcast - no job queue
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
