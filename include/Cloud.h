#ifndef CLOUD_H
#define CLOUD_H

#include "WebSocketClient.h"
#include <ArduinoJson.h>
#include <functional>

/**
 * @brief Cloud - Connects ESP32 devices to ESPWiFi Cloud
 * (cloud.espwifi.io)
 *
 * Establishes a persistent WebSocket connection to the cloud broker, allowing:
 * - Remote access to device from anywhere
 * - Bidirectional messaging between UI and device
 * - Automatic reconnection on network issues
 * - Device registration and claiming
 */
class Cloud {
public:
  struct Config {
    const char *baseUrl = "https://cloud.espwifi.io";
    const char *deviceId = nullptr;  // Device MAC or unique ID
    const char *authToken = nullptr; // Device authentication token
    const char *tunnel =
        "ws_control"; // Tunnel identifier (e.g., "ws_control", "ws_camera")
    bool autoReconnect = true;
    uint32_t reconnectDelay = 5000; // ms
    bool enabled = false;
  };

  // Message handler callback
  using OnMessageCb = std::function<void(JsonDocument &message)>;

  Cloud();
  ~Cloud();

  // Initialize and connect to cloud
  bool begin(const Config &config);

  // Connection management
  bool connect();
  void disconnect();
  bool reconnect();
  bool isConnected() const;

  // Send message to cloud (forwarded to connected UI clients)
  bool sendMessage(const JsonDocument &message);
  bool sendText(const char *message);
  bool sendBinary(const uint8_t *data, size_t len);

  // Set message handler (receives messages from UI clients)
  void onMessage(OnMessageCb callback) { onMessage_ = callback; }

  // Get device info
  const char *getDeviceId() const { return config_.deviceId; }
  const char *getTunnel() const { return config_.tunnel; }
  const char *getClaimCode() const { return claimCode_; }

private:
  Config config_;
  WebSocketClient ws_;
  OnMessageCb onMessage_ = nullptr;

  char wsUrl_[512] = {0}; // Increased from 256 to 512 for longer URLs
  char claimCode_[16] = {0};
  bool registered_ = false;

  // WebSocket event handlers
  void handleConnect();
  void handleDisconnect();
  void handleMessage(const uint8_t *data, size_t len, bool isBinary);

  // Generate claim code for device pairing
  void generateClaimCode();

  // Build WebSocket URL
  void buildWsUrl();
};

#endif // CLOUD_H
