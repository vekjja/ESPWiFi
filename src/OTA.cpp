// OTA.cpp - Stubbed for now
#include "ESPWiFi.h"

bool ESPWiFi::isOTAEnabled() {
  // Check if OTA partition exists
  const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
  return (partition != NULL);
}

void ESPWiFi::startOTA() { log(INFO, "🔄 OTA not fully implemented yet"); }

void ESPWiFi::handleOTAStart(void *req) {
  // Stub
}

void ESPWiFi::handleOTAUpdate(void *req, const std::string &filename,
                              size_t index, uint8_t *data, size_t len,
                              bool final) {
  // Stub
}

void ESPWiFi::handleOTAFileUpload(void *req, const std::string &filename,
                                  size_t index, uint8_t *data, size_t len,
                                  bool final) {
  // Stub
}

void ESPWiFi::resetOTAState() {
  otaInProgress = false;
  otaCurrentSize = 0;
  otaTotalSize = 0;
  otaErrorString = "";
  otaMD5Hash = "";
}

void ESPWiFi::srvOTA() {
  // Will implement later
}
