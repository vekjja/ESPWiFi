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
  wsConfig.disableCertVerify = config_.disableCertVerify;

  // Set up callbacks
  ws_.onConnect([this]() { handleConnect(); });
  ws_.onDisconnect([this]() { handleDisconnect(); });
  ws_.onMessage([this](const uint8_t *data, size_t len, bool isBinary) {
    handleMessage(data, len, isBinary);
  });

  // Start WebSocket connection
  if (!ws_.begin(wsConfig)) {
    ESP_LOGE(TAG, "Failed to start WebSocket client");
    return false;
  }

  return true;
}

bool Cloud::connect() { return ws_.connect(); }

void Cloud::disconnect() { ws_.disconnect(); }

bool Cloud::reconnect() { return ws_.reconnect(); }

bool Cloud::isConnected() const { return ws_.isConnected(); }

void Cloud::handleConnect() {
  ESP_LOGI(TAG, "Connected to cloud broker");
  registered_ =
      false; // Will be set to true when we receive "registered" message
}

void Cloud::handleDisconnect() {
  ESP_LOGI(TAG, "Disconnected from cloud broker");
  registered_ = false;
  uiWsUrl_[0] = '\0';
}

void Cloud::handleMessage(const uint8_t *data, size_t len, bool isBinary) {
  if (isBinary) {
    ESP_LOGW(TAG, "Received unexpected binary message (%zu bytes)", len);
    return;
  }

  // Parse JSON message
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);
  if (error) {
    ESP_LOGE(TAG, "Failed to parse message: %s", error.c_str());
    return;
  }

  const char *type = doc["type"];
  if (type == nullptr) {
    ESP_LOGW(TAG, "Message missing 'type' field");
    return;
  }

  // Handle registration confirmation
  if (strcmp(type, "registered") == 0) {
    registered_ = true;
    const char *uiWsUrl = doc["ui_ws_url"];
    if (uiWsUrl != nullptr) {
      snprintf(uiWsUrl_, sizeof(uiWsUrl_), "%s", uiWsUrl);
      ESP_LOGI(TAG, "Device registered, UI WebSocket URL: %s", uiWsUrl_);
    } else {
      ESP_LOGW(TAG, "Registered but no ui_ws_url provided");
    }
    return;
  }

  // Other message types handled by derived classes
}

void Cloud::generateClaimCode() {
  // Generate 8-character alphanumeric claim code
  const char charset[] =
      "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // Excluding ambiguous chars
  const size_t charsetLen = strlen(charset);

  for (size_t i = 0; i < 8; i++) {
    uint32_t rand = esp_random();
    claimCode_[i] = charset[rand % charsetLen];
  }
  claimCode_[8] = '\0';
}

void Cloud::buildWsUrl() {
  // Format:
  // wss://cloud.espwifi.io/ws/device/{deviceId}?tunnel={tunnel}&claim={claimCode}
  std::string protocol = "wss";
  std::string host = config_.baseUrl;

  // Extract protocol and host from baseUrl
  if (strncmp(host.c_str(), "https://", 8) == 0) {
    protocol = "wss";
    host = host.substr(8);
  } else if (strncmp(host.c_str(), "http://", 7) == 0) {
    protocol = "ws";
    host = host.substr(7);
  }

  // Remove trailing slash
  if (!host.empty() && host.back() == '/') {
    host.pop_back();
  }

  snprintf(wsUrl_, sizeof(wsUrl_),
           "%s://%s/ws/device/%s?tunnel=%s&claim=%s&announce=1&token=%s",
           protocol.c_str(), host.c_str(), config_.deviceId, config_.tunnel,
           claimCode_, config_.authToken ? config_.authToken : "");
}
