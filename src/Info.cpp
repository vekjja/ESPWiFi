#include "ESPWiFi.h"

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_wifi.h"

JsonDocument ESPWiFi::buildInfoJson(bool yieldForWatchdog) {
  JsonDocument jsonDoc;

  const auto maybeYield = [&]() {
    if (yieldForWatchdog) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  };

  // Uptime in seconds
  jsonDoc["uptime"] = millis() / 1000;

  // IP address
  jsonDoc["ip"] = ipAddress();

  jsonDoc["mac"] = getMacAddress();

  // Hostname
  std::string hostname = getHostname();
  jsonDoc["hostname"] = hostname;

  // AP SSID (same derivation as AP start)
  std::string ap_ssid = config["wifi"]["ap"]["ssid"].as<std::string>();
  jsonDoc["ap_ssid"] = ap_ssid + "-" + hostname;

  // mDNS hostname (best-effort)
  std::string deviceName = config["deviceName"].as<std::string>();
  jsonDoc["mdns"] = deviceName + ".local";

  // Cloud status (config + runtime)

  maybeYield();

  // Chip model and SDK version
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  char chip_model[32];
  if (chip_info.model == CHIP_ESP32C3) {
    snprintf(chip_model, sizeof(chip_model), "ESP32-C3");
  } else if (chip_info.model == CHIP_ESP32) {
    snprintf(chip_model, sizeof(chip_model), "ESP32");
  } else if (chip_info.model == CHIP_ESP32S2) {
    snprintf(chip_model, sizeof(chip_model), "ESP32-S2");
  } else if (chip_info.model == CHIP_ESP32S3) {
    snprintf(chip_model, sizeof(chip_model), "ESP32-S3");
  } else {
    snprintf(chip_model, sizeof(chip_model), "ESP32-Unknown");
  }
  jsonDoc["chip"] = std::string(chip_model);
  jsonDoc["fw_version"] = version();
  jsonDoc["sdk_version"] = std::string(esp_get_idf_version());

  // Heap information
  const size_t free_heap = esp_get_free_heap_size();
  const size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
  jsonDoc["free_heap"] = free_heap;
  jsonDoc["total_heap"] = total_heap;
  jsonDoc["used_heap"] = total_heap - free_heap;

  maybeYield();

  // WiFi connection status and info
  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    char ssid_str[33];
    memcpy(ssid_str, ap_info.ssid, 32);
    ssid_str[32] = '\0';
    jsonDoc["client_ssid"] = std::string(ssid_str);
    jsonDoc["rssi"] = ap_info.rssi;
  }

  // WiFi power settings and applied values
  {
    JsonDocument powerInfo = getWiFiPowerInfo();
    if (!powerInfo.isNull() && powerInfo.size() > 0) {
      jsonDoc["wifi_power"] = powerInfo;
    }
  }

  maybeYield();

  // LittleFS storage information
  if (lfs != nullptr) {
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
    jsonDoc["lfs_free"] = freeBytes;
    jsonDoc["lfs_used"] = usedBytes;
    jsonDoc["lfs_total"] = totalBytes;
  } else {
    jsonDoc["lfs_free"] = 0;
    jsonDoc["lfs_used"] = 0;
    jsonDoc["lfs_total"] = 0;
  }

  maybeYield();

  // SD card storage information if available
  if (sdCard != nullptr) {
    size_t sdTotalBytes, sdUsedBytes, sdFreeBytes;
    getStorageInfo("sd", sdTotalBytes, sdUsedBytes, sdFreeBytes);
    jsonDoc["sd_free"] = sdFreeBytes;
    jsonDoc["sd_used"] = sdUsedBytes;
    jsonDoc["sd_total"] = sdTotalBytes;
  }

  return jsonDoc;
}
