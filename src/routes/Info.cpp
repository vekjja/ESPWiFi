#ifndef ESPWiFi_SRV_INFO
#define ESPWiFi_SRV_INFO

#include "ESPWiFi.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_wifi.h"

void ESPWiFi::srvInfo() {
  // Info endpoint
  (void)registerRoute(
      "/api/info", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        JsonDocument jsonDoc;

        // Uptime in seconds
        jsonDoc["uptime"] = millis() / 1000;

        // IP address - get from current network interface
        std::string ip = espwifi->ipAddress();
        jsonDoc["ip"] = ip;

        // MAC address - try WiFi interface first, fallback to reading MAC
        // directly
        uint8_t mac[6];
        esp_err_t mac_ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
        if (mac_ret != ESP_OK) {
          // Fallback: read MAC directly from hardware
          mac_ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
        }
        if (mac_ret == ESP_OK) {
          char macStr[18];
          snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
          jsonDoc["mac"] = std::string(macStr);
        } else {
          jsonDoc["mac"] = "";
        }

        // Hostname
        std::string hostname = espwifi->getHostname();
        jsonDoc["hostname"] = hostname;

        // AP SSID - construct from config the same way as when starting AP
        std::string ap_ssid =
            espwifi->config["wifi"]["ap"]["ssid"].as<std::string>();
        jsonDoc["ap_ssid"] = ap_ssid + "-" + hostname;

        // mDNS
        std::string deviceName =
            espwifi->config["deviceName"].as<std::string>();
        jsonDoc["mdns"] = deviceName + ".local";

        // Yield to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(10));

        // Chip model and SDK version
        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);
        char chip_model[32];
        // Format chip model based on chip_info.model
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
        jsonDoc["sdk_version"] = std::string(esp_get_idf_version());

        // Heap information
        size_t free_heap = esp_get_free_heap_size();
        size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
        jsonDoc["free_heap"] = free_heap;
        jsonDoc["total_heap"] = total_heap;
        jsonDoc["used_heap"] = total_heap - free_heap;

        // Yield to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(10));

        // WiFi connection status and info
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
          // WiFi is connected as station
          char ssid_str[33];
          memcpy(ssid_str, ap_info.ssid, 32);
          ssid_str[32] = '\0';
          jsonDoc["client_ssid"] = std::string(ssid_str);
          jsonDoc["rssi"] = ap_info.rssi;
        }

        // Yield to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(10));

        // LittleFS storage information
        if (espwifi->lfs != nullptr) {
          size_t totalBytes, usedBytes, freeBytes;
          espwifi->getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
          jsonDoc["littlefs_free"] = freeBytes;
          jsonDoc["littlefs_used"] = usedBytes;
          jsonDoc["littlefs_total"] = totalBytes;
        } else {
          jsonDoc["littlefs_free"] = 0;
          jsonDoc["littlefs_used"] = 0;
          jsonDoc["littlefs_total"] = 0;
        }

        // Yield to prevent watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(10));

        // SD card storage information if available
        if (espwifi->sdCard != nullptr) {
          size_t sdTotalBytes, sdUsedBytes, sdFreeBytes;
          espwifi->getStorageInfo("sd", sdTotalBytes, sdUsedBytes, sdFreeBytes);
          jsonDoc["sd_free"] = sdFreeBytes;
          jsonDoc["sd_used"] = sdUsedBytes;
          jsonDoc["sd_total"] = sdTotalBytes;
        }

        // Serialize JSON to string
        std::string jsonResponse;
        serializeJson(jsonDoc, jsonResponse);

        (void)espwifi->sendJsonResponse(req, 200, jsonResponse, &clientInfo);
        return ESP_OK;
      });
}

#endif // ESPWiFi_SRV_INFO
