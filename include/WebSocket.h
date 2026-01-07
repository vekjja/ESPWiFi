#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

class ESPWiFi;

#ifndef CONFIG_HTTPD_WS_SUPPORT
// When CONFIG_HTTPD_WS_SUPPORT is disabled, esp_http_server.h may not declare
// websocket types. Provide tiny fallbacks so this header stays buildable.
typedef int httpd_ws_type_t;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_TEXT = 0;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_BINARY = 1;
static constexpr httpd_ws_type_t HTTPD_WS_TYPE_CLOSE = 2;
#endif

class WebSocket {
private:
  ESPWiFi *espWifi_ = nullptr;

  static constexpr size_t kMaxUriLen = 64;
  char uri_[kMaxUriLen] = {0};

  static constexpr size_t kMaxClients = 8; // matches/overlaps our httpd config
  int clientFds_[kMaxClients] = {0};
  size_t clientCount_ = 0;

  size_t maxMessageLen_ = 1024;
  size_t maxBroadcastLen_ = 8192;

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

  static void getRemoteInfo_(int fd, char *outIp, size_t outIpLen,
                             uint16_t *outPort);
  static esp_err_t wsHandlerTrampoline(httpd_req_t *req);
  void addClient_(int fd);
  void removeClient_(int fd);
  esp_err_t handleWsRequest(httpd_req_t *req);

  struct BroadcastJob;
  static void broadcastWorkTrampoline(void *arg);
  void broadcastNow_(httpd_ws_type_t type, const uint8_t *payload, size_t len);

public:
  WebSocket() = default;
  ~WebSocket();

  // Initialize and register the WS endpoint.
  // Note: `uri` is copied into a fixed buffer (bounded RAM).
  bool begin(const char *uri, ESPWiFi *espWifi, OnMessageCb onMessage = nullptr,
             OnConnectCb onConnect = nullptr,
             OnDisconnectCb onDisconnect = nullptr, size_t maxMessageLen = 1024,
             size_t maxBroadcastLen = 8192, bool requireAuth = false);

  operator bool() const { return started_; }

  size_t numClients() const { return clientCount_; }

  // Queue a broadcast into the HTTP server task for thread-safety and to keep
  // callers snappy (user-perceived performance).
  esp_err_t textAll(const char *message);
  esp_err_t textAll(const char *message, size_t len);

  esp_err_t binaryAll(const uint8_t *data, size_t len);

  void closeAll();
};

#endif