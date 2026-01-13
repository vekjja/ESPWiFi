#ifndef CLOUD_H
#define CLOUD_H

#include "WebSocketClient.h"
#include <ArduinoJson.h>
#include <functional>

/**
 * @brief Cloud - Base class for ESP32 cloud connections to ESPWiFi Cloud
 * (cloud.espwifi.io)
 *
 * Provides common connection management, authentication, and device
 * registration. Specialized by CloudCtl (JSON control) and CloudMedia (binary
 * streaming).
 */
class Cloud {
public:
  struct Config {
    const char *baseUrl = "https://cloud.espwifi.io";
    const char *deviceId = nullptr;  // Device MAC or unique ID
    const char *authToken = nullptr; // Device authentication token
    const char *tunnel =
        "ws_control"; // Tunnel identifier (e.g., "ws_control", "ws_media")
    bool autoReconnect = true;
    uint32_t reconnectDelay = 5000; // ms
    bool enabled = false;
    bool disableCertVerify =
        false; // Disable TLS cert verification (for testing)
  };

  Cloud();
  virtual ~Cloud();

  // Initialize and connect to cloud
  bool begin(const Config &config);

  // Connection management
  bool connect();
  void disconnect();
  bool reconnect();
  bool isConnected() const;

  // Get device info
  const char *getDeviceId() const { return config_.deviceId; }
  const char *getTunnel() const { return config_.tunnel; }
  const char *getClaimCode() const { return claimCode_; }
  const char *getUiWebSocketUrl() const { return uiWsUrl_; }
  bool isRegistered() const { return registered_; }

protected:
  Config config_;
  WebSocketClient ws_;

  char wsUrl_[512] = {0}; // WebSocket URL for cloud connection
  char claimCode_[16] = {0};
  char uiWsUrl_[512] = {0}; // UI WebSocket URL from cloud broker
  bool registered_ = false;

  // WebSocket event handlers (can be overridden by derived classes)
  virtual void handleConnect();
  virtual void handleDisconnect();
  virtual void handleMessage(const uint8_t *data, size_t len, bool isBinary);

  // Generate claim code for device pairing
  void generateClaimCode();

  // Build WebSocket URL
  void buildWsUrl();
};

#endif // CLOUD_H
