#include "CloudMedia.h"
#include "esp_log.h"

static const char *TAG = "CloudMedia";

CloudMedia::CloudMedia() : Cloud() {}

CloudMedia::~CloudMedia() {}

bool CloudMedia::sendBinary(const uint8_t *data, size_t len) {
  if (!ws_.isConnected()) {
    ESP_LOGV(TAG, "Not connected to cloud");
    return false;
  }

  if (!registered_) {
    ESP_LOGV(TAG, "Not registered with cloud");
    return false;
  }

  return ws_.sendBinary(data, len);
}

void CloudMedia::handleMessage(const uint8_t *data, size_t len, bool isBinary) {
  // Handle registration messages from base class
  if (!isBinary) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, data, len);
    if (!error) {
      const char *type = doc["type"];
      if (type != nullptr && strcmp(type, "registered") == 0) {
        Cloud::handleMessage(data, len, isBinary);
        return;
      }
    }
  }

  // Media tunnel is one-way (device -> UI), ignore other messages
  ESP_LOGV(TAG, "Ignoring incoming message on media tunnel");
}
