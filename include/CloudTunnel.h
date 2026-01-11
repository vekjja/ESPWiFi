#ifndef CLOUDTUNNEL_H
#define CLOUDTUNNEL_H

#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"

#ifndef CONFIG_HTTPD_WS_SUPPORT
typedef int httpd_ws_type_t;
#endif

// CloudTunnel: Manages a cloud WebSocket connection for tunneling
// - Connects to a cloud broker (e.g., wss://tnl.espwifi.io)
// - Acts as a synthetic WebSocket client
// - Forwards messages between cloud and local WebSocket handlers
class CloudTunnel {
public:
  // Synthetic fd used to represent the cloud connection in callbacks
  static constexpr int kCloudClientFd = -7777;

  // Message callback: called when cloud receives a message
  // Parameters: type, data, len, userCtx
  using OnMessageCb = void (*)(httpd_ws_type_t type, const uint8_t *data,
                               size_t len, void *userCtx);

  // Connection state callback
  using OnConnectCb = void (*)(void *userCtx);
  using OnDisconnectCb = void (*)(void *userCtx);

private:
  void *userCtx_ = nullptr;

  static constexpr size_t kMaxBaseUrlLen = 160;
  static constexpr size_t kMaxDeviceIdLen = 64;
  static constexpr size_t kMaxTokenLen = 96;
  static constexpr size_t kMaxTunnelKeyLen = 64;

  char baseUrl_[kMaxBaseUrlLen] = {0};
  char deviceId_[kMaxDeviceIdLen] = {0};
  char token_[kMaxTokenLen] = {0};
  char tunnelKey_[kMaxTunnelKeyLen] = {0};

  // Last registration details from broker
  static constexpr size_t kMaxRegisteredUrlLen = 220;
  char uiWSURL_[kMaxRegisteredUrlLen] = {0};
  char deviceWSURL_[kMaxRegisteredUrlLen] = {0};
  volatile uint32_t registeredAtMs_ = 0;
  volatile bool uiConnected_ = false;

  volatile bool enabled_ = false;
  volatile bool connected_ = false;

  // Opaque handles
  void *client_ = nullptr; // esp_websocket_client_handle_t
  void *task_ = nullptr;   // TaskHandle_t
  void *mutex_ = nullptr;  // SemaphoreHandle_t

  OnMessageCb onMessage_ = nullptr;
  OnConnectCb onConnect_ = nullptr;
  OnDisconnectCb onDisconnect_ = nullptr;

  void startTask_();
  void stop_();
  static void taskTrampoline_(void *arg);
  static void eventHandler_(void *handler_args, esp_event_base_t base,
                            int32_t event_id, void *event_data);

public:
  CloudTunnel() = default;
  ~CloudTunnel();

  // Configure cloud tunnel
  // - baseUrl: e.g., "wss://tnl.espwifi.io"
  // - deviceId: device identifier
  // - token: auth token
  // - tunnelKey: tunnel identifier (e.g., "ws_control")
  void configure(const char *baseUrl, const char *deviceId, const char *token,
                 const char *tunnelKey);

  // Enable/disable cloud tunnel
  void setEnabled(bool enabled);

  // Send a frame to the cloud
  esp_err_t sendText(const char *message, size_t len);
  esp_err_t sendBinary(const uint8_t *data, size_t len);

  // Set callbacks
  void setCallbacks(OnMessageCb onMessage, OnConnectCb onConnect,
                    OnDisconnectCb onDisconnect, void *userCtx);

  // Status
  bool enabled() const { return enabled_; }
  bool connected() const { return connected_; }
  bool uiConnected() const { return uiConnected_; }
  const char *uiWSURL() const { return uiWSURL_; }
  const char *deviceWSURL() const { return deviceWSURL_; }
  uint32_t registeredAtMs() const { return registeredAtMs_; }
};

#endif
