#ifndef WEBSOCKETCLIENT_H
#define WEBSOCKETCLIENT_H

#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>
#include <functional>

#include "esp_err.h"
#include "esp_websocket_client.h"

class WebSocketClient {
public:
  // Event types for callbacks
  enum EventType {
    EVENT_CONNECTED,
    EVENT_DISCONNECTED,
    EVENT_TEXT,
    EVENT_BINARY,
    EVENT_PING,
    EVENT_PONG,
    EVENT_ERROR
  };

  // Callback signatures
  using OnEventCb =
      std::function<void(EventType event, const uint8_t *data, size_t len)>;
  using OnConnectCb = std::function<void()>;
  using OnDisconnectCb = std::function<void()>;
  using OnMessageCb =
      std::function<void(const uint8_t *data, size_t len, bool isBinary)>;
  using OnErrorCb = std::function<void(esp_err_t error)>;

private:
  esp_websocket_client_handle_t client_ = nullptr;
  bool connected_ = false;
  bool autoReconnect_ = false;

  char uri_[256] = {0};

  // Callbacks
  OnConnectCb onConnect_ = nullptr;
  OnDisconnectCb onDisconnect_ = nullptr;
  OnMessageCb onMessage_ = nullptr;
  OnErrorCb onError_ = nullptr;

  // Buffer for incoming data
  static constexpr size_t kMaxMessageLen = 8192;
  uint8_t *recvBuffer_ = nullptr;
  size_t recvBufferSize_ = 0;

  // Reconnection settings
  uint32_t reconnectDelay_ = 5000;    // ms
  uint32_t maxReconnectAttempts_ = 0; // 0 = infinite
  uint32_t reconnectAttempts_ = 0;

  // Authentication
  char authHeader_[256] = {0};

  // Event handler (static trampoline)
  static void eventHandlerTrampoline(void *handlerArgs, esp_event_base_t base,
                                     int32_t eventId, void *eventData);

  // Instance event handler
  void handleEvent(esp_event_base_t base, int32_t eventId, void *eventData);

  // Reconnection logic
  void scheduleReconnect();

public:
  WebSocketClient();
  ~WebSocketClient();

  // Configuration structure
  struct Config {
    const char *uri = nullptr;
    const char *subprotocol = nullptr;
    const char *userAgent = nullptr;
    const char *authUser = nullptr;
    const char *authPass = nullptr;
    const char *authToken = nullptr; // Bearer token
    const char *certPem = nullptr;   // Server certificate for TLS
    bool autoReconnect = false;
    uint32_t reconnectDelay = 5000;
    uint32_t maxReconnectAttempts = 0; // 0 = infinite
    size_t bufferSize = 4096;
    uint32_t pingInterval = 10000; // ms, 0 = disable
    uint32_t timeout = 10000;      // ms
    bool disableCertVerify = false;
  };

  // Initialize and connect to WebSocket server
  bool begin(const Config &config);
  bool begin(const char *uri); // Simple begin with defaults

  // Connect/disconnect
  bool connect();
  void disconnect();
  bool reconnect();

  // Connection state
  bool isConnected() const { return connected_; }
  operator bool() const { return connected_; }

  // Send operations
  esp_err_t sendText(const char *message);
  esp_err_t sendText(const char *message, size_t len);
  esp_err_t sendBinary(const uint8_t *data, size_t len);
  esp_err_t sendPing();
  esp_err_t sendPong();

  // Set callbacks
  void onConnect(OnConnectCb callback) { onConnect_ = callback; }
  void onDisconnect(OnDisconnectCb callback) { onDisconnect_ = callback; }
  void onMessage(OnMessageCb callback) { onMessage_ = callback; }
  void onError(OnErrorCb callback) { onError_ = callback; }

  // Get connection info
  const char *getUri() const { return uri_; }
  uint32_t getReconnectAttempts() const { return reconnectAttempts_; }

  // Close the connection
  void close();
};

#endif // WEBSOCKETCLIENT_H
