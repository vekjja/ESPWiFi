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
// Internal job structures for async operations
// ============================================================================

struct WebSocket::BroadcastJob {
  WebSocket *ws;
  httpd_ws_type_t type;
  size_t len;
  // payload bytes follow
};

struct WebSocket::SendJob {
  WebSocket *ws;
  int fd;
  httpd_ws_type_t type;
  size_t len;
  // payload bytes follow
};

// ============================================================================
// Async send helpers (queued via httpd_queue_work)
// ============================================================================

void WebSocket::sendToWorkTrampoline(void *arg) {
  if (arg == nullptr) {
    return;
  }
  WebSocket::SendJob *job = static_cast<WebSocket::SendJob *>(arg);
  WebSocket *ws = job->ws;
  const uint8_t *payload = (const uint8_t *)(job + 1);

#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (ws != nullptr && ws->server_ != nullptr) {
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = job->type;
    frame.payload = (uint8_t *)payload;
    frame.len = job->len;

    esp_err_t err = httpd_ws_send_frame_async(ws->server_, job->fd, &frame);
    if (err != ESP_OK) {
      // Client disconnected; remove from tracked list
      ws->removeClient_(job->fd);
      if (ws->onDisconnect_) {
        ws->onDisconnect_(ws, job->fd, ws->userCtx_);
      }
    }
  }
#else
  (void)ws;
  (void)payload;
#endif

  free(job);
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
  if (server_ == nullptr) {
    return;
  }

  httpd_ws_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.type = type;
  frame.payload = (uint8_t *)payload;
  frame.len = len;

  // Send to all tracked clients; remove dead ones
  for (size_t i = 0; i < clientCount_;) {
    int fd = clientFds_[i];
    esp_err_t err = httpd_ws_send_frame_async(server_, fd, &frame);
    if (err != ESP_OK) {
      if (onDisconnect_) {
        onDisconnect_(this, fd, userCtx_);
      }
      removeClient_(fd);
      continue; // don't increment i
    }
    ++i;
  }
#else
  (void)type;
  (void)payload;
  (void)len;
#endif
}

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
  for (size_t i = 0; i < clientCount_; ++i) {
    if (clientFds_[i] == fd) {
      return; // already tracked
    }
  }
  if (clientCount_ >= kMaxClients) {
    // Drop oldest to stay bounded
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

    // Optional auth check
    if (requireAuth_ && authCheck_ != nullptr) {
      if (!authCheck_(req, userCtx_)) {
        // Close session immediately (handshake already upgraded)
        (void)httpd_sess_trigger_close(server_, fd);
        return ESP_OK;
      }
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
                      AuthCheckFn authCheck) {
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
  if (message == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    return ESP_OK;
  }
  if (len > maxBroadcastLen_) {
    return ESP_ERR_INVALID_SIZE;
  }

#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)clientFd;
  (void)message;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || server_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  SendJob *job = (SendJob *)malloc(sizeof(SendJob) + len);
  if (job == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  job->ws = this;
  job->fd = clientFd;
  job->type = HTTPD_WS_TYPE_TEXT;
  job->len = len;
  memcpy((uint8_t *)(job + 1), message, len);

  return httpd_queue_work(server_, &WebSocket::sendToWorkTrampoline, job);
#endif
}

esp_err_t WebSocket::sendBinary(int clientFd, const uint8_t *data, size_t len) {
  if (data == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (len == 0) {
    return ESP_OK;
  }
  if (len > maxBroadcastLen_) {
    return ESP_ERR_INVALID_SIZE;
  }

#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)clientFd;
  (void)data;
  (void)len;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (!started_ || server_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  SendJob *job = (SendJob *)malloc(sizeof(SendJob) + len);
  if (job == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  job->ws = this;
  job->fd = clientFd;
  job->type = HTTPD_WS_TYPE_BINARY;
  job->len = len;
  memcpy((uint8_t *)(job + 1), data, len);

  return httpd_queue_work(server_, &WebSocket::sendToWorkTrampoline, job);
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

  BroadcastJob *job = (BroadcastJob *)malloc(sizeof(BroadcastJob) + len);
  if (job == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  job->ws = this;
  job->type = HTTPD_WS_TYPE_TEXT;
  job->len = len;
  memcpy((uint8_t *)(job + 1), message, len);

  return httpd_queue_work(server_, &WebSocket::broadcastWorkTrampoline, job);
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

  BroadcastJob *job = (BroadcastJob *)malloc(sizeof(BroadcastJob) + len);
  if (job == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  job->ws = this;
  job->type = HTTPD_WS_TYPE_BINARY;
  job->len = len;
  memcpy((uint8_t *)(job + 1), data, len);

  return httpd_queue_work(server_, &WebSocket::broadcastWorkTrampoline, job);
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
