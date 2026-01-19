#include "CloudCtl.h"
#include "esp_log.h"
#include <string>

static const char *TAG = "CloudCtl";

CloudCtl::CloudCtl() : Cloud() {}

CloudCtl::~CloudCtl() {}

bool CloudCtl::sendMessage(const JsonDocument &message) {
  if (!ws_.isConnected()) {
    ESP_LOGW(TAG, "Not connected to cloud");
    return false;
  }
  std::string output;
  serializeJson(message, output);
  return sendText(output.c_str());
}

bool CloudCtl::sendText(const char *message) {
  if (!ws_.isConnected()) {
    ESP_LOGW(TAG, "Not connected to cloud");
    return false;
  }
  return ws_.sendText(message);
}

void CloudCtl::handleMessage(const uint8_t *data, size_t len, bool isBinary) {
  if (isBinary) {
    ESP_LOGW(TAG, "Received unexpected binary message");
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

  // Handle cloud-specific messages (messages with 'type' field)
  if (type != nullptr) {
    // Check for registration message (handled by base class)
    if (strcmp(type, "registered") == 0) {
      Cloud::handleMessage(data, len, isBinary);
      return;
    }

    // UI connection status messages
    if (strcmp(type, "ui_connected") == 0) {
      ESP_LOGI(TAG, "UI client connected via cloud");
      return;
    }

    if (strcmp(type, "ui_disconnected") == 0) {
      ESP_LOGI(TAG, "UI client disconnected from cloud");
      return;
    }
  }

  // Forward all device control messages (with or without 'type') to application
  // handler
  if (onMessage_) {
    ESP_LOGD(TAG, "Forwarding message to application handler");
    onMessage_(doc);
  }
}
