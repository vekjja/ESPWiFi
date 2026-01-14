#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

#ifndef CONFIG_HTTPD_WS_SUPPORT
// When CONFIG_HTTPD_WS_SUPPORT is disabled, esp_http_server.h may not declare
// websocket types. Provide tiny fallbacks so this header stays buildable.
typedef int httpd_ws_type_t;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_TEXT = 0;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_BINARY = 1;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_CLOSE = 2;
#endif

class WebSocket {
public:
  // Callbacks use void* for user context
  using OnConnectCb = void (*)(WebSocket *ws, int clientFd, void *userCtx);
  using OnDisconnectCb = void (*)(WebSocket *ws, int clientFd, void *userCtx);
  using OnMessageCb = void (*)(WebSocket *ws, int clientFd,
                               httpd_ws_type_t type, const uint8_t *data,
                               size_t len, void *userCtx);

private:
  void *userCtx_ = nullptr;

  static constexpr size_t kMaxUriLen = 64;
  char uri_[kMaxUriLen] = {0};

  static constexpr size_t kMaxClients = 8;
  int clientFds_[kMaxClients] = {0};
  size_t clientCount_ = 0;
  size_t maxClients_ = kMaxClients; // Per-endpoint client limit

  size_t maxMessageLen_ = 1024;
  size_t maxBroadcastLen_ = 8192;

  OnConnectCb onConnect_ = nullptr;
  OnDisconnectCb onDisconnect_ = nullptr;
  OnMessageCb onMessage_ = nullptr;

  bool started_ = false;
  bool requireAuth_ = false;
  httpd_handle_t server_ = nullptr;

  // Auth helper (optional; nullptr = no auth)
  using AuthCheckFn = bool (*)(httpd_req_t *req, void *userCtx);
  AuthCheckFn authCheck_ = nullptr;

  static void getRemoteInfo_(int fd, char *outIp, size_t outIpLen,
                             uint16_t *outPort);
  static esp_err_t wsHandlerTrampoline(httpd_req_t *req);
  void addClient_(int fd);
  void removeClient_(int fd);
  esp_err_t handleWsRequest(httpd_req_t *req);

public:
  WebSocket() = default;
  ~WebSocket();

  // Initialize and register the WS endpoint
  // - uri: WebSocket endpoint path (e.g., "/ws/control")
  // - server: ESP-IDF httpd handle
  // - userCtx: User context passed to callbacks
  // - authCheck: Optional auth function (nullptr = no auth)
  // - maxClients: Maximum clients allowed (default: 8, use 1 for single-client)
  bool begin(const char *uri, httpd_handle_t server, void *userCtx,
             OnMessageCb onMessage = nullptr, OnConnectCb onConnect = nullptr,
             OnDisconnectCb onDisconnect = nullptr, size_t maxMessageLen = 1024,
             size_t maxBroadcastLen = 8192, bool requireAuth = false,
             AuthCheckFn authCheck = nullptr, size_t maxClients = 8);

  operator bool() const { return started_; }

  // LAN client count
  size_t clientCount() const { return clientCount_; }

  // Send text to a specific client
  esp_err_t sendText(int clientFd, const char *message, size_t len);
  esp_err_t sendText(int clientFd, const char *message);

  // Send binary to a specific client
  esp_err_t sendBinary(int clientFd, const uint8_t *data, size_t len);

  // Broadcast text to all connected clients
  esp_err_t broadcastText(const char *message, size_t len);
  esp_err_t broadcastText(const char *message);

  // Broadcast binary to all connected clients
  esp_err_t broadcastBinary(const uint8_t *data, size_t len);

  // Close all connections
  void closeAll();

  // Get client FDs (for external management, e.g., CloudTunnel)
  const int *getClientFds() const { return clientFds_; }
};

#endif
