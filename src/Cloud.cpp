#include "Cloud.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include <cstdio>
#include <cstring>
#include <string>

static const char *TAG = "Cloud";

Cloud::Cloud() {}

Cloud::~Cloud() { disconnect(); }

bool Cloud::begin(const Config &config) {
  if (!config.enabled) {
    ESP_LOGI(TAG, "Cloud client disabled");
    return true;
  }

  if (config.baseUrl == nullptr || config.deviceId == nullptr) {
    ESP_LOGE(TAG, "baseUrl and deviceId are required");
    return false;
  }

  // Store configuration
  config_ = config;

  // Generate claim code for device pairing
  generateClaimCode();

  // Build WebSocket URL
  buildWsUrl();

  ESP_LOGI(TAG, "Initializing cloud client");
  ESP_LOGI(TAG, "Device ID: %s", config_.deviceId);
  ESP_LOGI(TAG, "Tunnel: %s", config_.tunnel);
  ESP_LOGI(TAG, "Claim Code: %s", claimCode_);

  // Configure WebSocket client
  WebSocketClient::Config wsConfig;
  wsConfig.uri = wsUrl_;
  wsConfig.authToken = config_.authToken;
  wsConfig.autoReconnect = config_.autoReconnect;
  wsConfig.reconnectDelay = config_.reconnectDelay;
  wsConfig.pingInterval = 30000; // 30 seconds
  wsConfig.bufferSize = 8192;

  // Set up callbacks
  ws_.onConnect([this]() { handleConnect(); });
  ws_.onDisconnect([this]() { handleDisconnect(); });
  ws_.onMessage([this](const uint8_t *data, size_t len, bool isBinary) {
    handleMessage(data, len, isBinary);
  });

  // Start connection
  return ws_.begin(wsConfig);
}

bool Cloud::connect() { return ws_.connect(); }

void Cloud::disconnect() {
  ws_.disconnect();
  registered_ = false;
}

bool Cloud::reconnect() { return ws_.reconnect(); }

bool Cloud::isConnected() const { return ws_.isConnected() && registered_; }

bool Cloud::sendMessage(const JsonDocument &message) {
  if (!ws_.isConnected()) {
    ESP_LOGW(TAG, "Not connected to cloud");
    return false;
  }

  // Use std::string for ESP-IDF (not Arduino String)
  std::string output;
  serializeJson(message, output);
  return sendText(output.c_str());
}

bool Cloud::sendText(const char *message) {
  if (!ws_.isConnected()) {
    return false;
  }

  esp_err_t err = ws_.sendText(message);
  return (err == ESP_OK);
}

bool Cloud::sendBinary(const uint8_t *data, size_t len) {
  if (!ws_.isConnected()) {
    return false;
  }

  esp_err_t err = ws_.sendBinary(data, len);
  return (err == ESP_OK);
}

void Cloud::handleConnect() {
  ESP_LOGI(TAG, "Connected to cloud at %s", config_.baseUrl);
  registered_ = false;

  // Note: The cloud server will send a "registered" message
  // after we connect, which we'll handle in handleMessage
}

void Cloud::handleDisconnect() {
  ESP_LOGI(TAG, "Disconnected from cloud");
  registered_ = false;
}

void Cloud::handleMessage(const uint8_t *data, size_t len, bool isBinary) {
  if (isBinary) {
    // Forward binary messages to handler if set
    if (onMessage_) {
      JsonDocument doc;
      doc["type"] = "binary";
      doc["length"] = len;
      onMessage_(doc);
    }
    return;
  }

  // Parse JSON message
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    ESP_LOGW(TAG, "Failed to parse message: %s", error.c_str());
    return;
  }

  const char *type = doc["type"];
  if (type == nullptr) {
    ESP_LOGW(TAG, "Message missing 'type' field");
    return;
  }

  ESP_LOGD(TAG, "Received message type: %s", type);

  // Handle cloud-specific messages
  if (strcmp(type, "registered") == 0) {
    registered_ = true;

    // Store UI WebSocket URL from broker
    const char *uiUrl = doc["ui_ws_url"];
    if (uiUrl) {
      strncpy(uiWsUrl_, uiUrl, sizeof(uiWsUrl_) - 1);
      uiWsUrl_[sizeof(uiWsUrl_) - 1] = '\0';
    }

    ESP_LOGI(TAG, "Device registered with cloud");
    ESP_LOGI(TAG, "UI WebSocket URL: %s", uiWsUrl_);
    ESP_LOGI(TAG, "Share claim code with users: %s", claimCode_);
    return;
  }

  if (strcmp(type, "ui_connected") == 0) {
    ESP_LOGI(TAG, "UI client connected");
    // Optionally notify application that UI is connected
  }

  if (strcmp(type, "ui_disconnected") == 0) {
    ESP_LOGI(TAG, "UI client disconnected");
    // Optionally notify application that UI is disconnected
  }

  // Forward all messages to application handler
  if (onMessage_) {
    onMessage_(doc);
  }
}

void Cloud::generateClaimCode() {
  // Generate a random 6-character alphanumeric claim code
  const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // Avoid 0, O, 1, I
  const size_t charsLen = sizeof(chars) - 1;

  for (size_t i = 0; i < 6; i++) {
    uint32_t random = esp_random();
    claimCode_[i] = chars[random % charsLen];
  }
  claimCode_[6] = '\0';
}

void Cloud::buildWsUrl() {
  // Convert https:// to wss://
  char wsBaseUrl[256];
  if (strncmp(config_.baseUrl, "https://", 8) == 0) {
    snprintf(wsBaseUrl, sizeof(wsBaseUrl), "wss://%s", config_.baseUrl + 8);
  } else if (strncmp(config_.baseUrl, "http://", 7) == 0) {
    snprintf(wsBaseUrl, sizeof(wsBaseUrl), "ws://%s", config_.baseUrl + 7);
  } else {
    // Assume wss:// if no protocol specified
    snprintf(wsBaseUrl, sizeof(wsBaseUrl), "wss://%s", config_.baseUrl);
  }

  // Build:
  // wss://cloud.espwifi.io/ws/device/{deviceId}?tunnel={tunnel}&claim={claimCode}
  if (config_.tunnel && strlen(config_.tunnel) > 0) {
    snprintf(wsUrl_, sizeof(wsUrl_),
             "%s/ws/device/%s?tunnel=%s&claim=%s&announce=1", wsBaseUrl,
             config_.deviceId, config_.tunnel, claimCode_);
  } else {
    snprintf(wsUrl_, sizeof(wsUrl_), "%s/ws/device/%s?claim=%s&announce=1",
             wsBaseUrl, config_.deviceId, claimCode_);
  }

  ESP_LOGD(TAG, "WebSocket URL: %s", wsUrl_);
}
