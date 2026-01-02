#ifndef ESPWiFi_MDNS
#define ESPWiFi_MDNS

#include "ESPWiFi.h"
#include <mdns.h>

/**
 * @brief Initializes and starts the mDNS responder service.
 *
 * This function sets up mDNS (Multicast DNS) to enable network service
 * discovery on the local network. It advertises the device as:
 * - <hostname>.local for web access
 * - HTTP service (_http._tcp) on port 80
 * - WebSocket service (_ws._tcp) on port 80
 * - Arduino OTA service (_arduino._tcp) on port 3232
 *
 * The function follows ESP-IDF best practices:
 * - Validates configuration before initialization
 * - Uses graceful error handling (logs but doesn't abort)
 * - Checks return codes for all mDNS operations
 * - Cleans up resources on critical failures
 *
 * @note Called from ESPWiFi::start() after WiFi initialization
 * @note Respects config["wifi"]["enabled"] and config["wifi"]["mdns"]
 * @note Requires espressif/mdns component (v1.0.0+) from ESP Component Registry
 */
void ESPWiFi::startMDNS() {
  // Skip mDNS if WiFi is not enabled
  if (!config["wifi"]["enabled"].as<bool>()) {
    log(DEBUG, "🏷️ mDNS: Skipped (WiFi disabled)");
    return;
  }

  // Check if mDNS is explicitly disabled in config (defaults to enabled)
  if (config["wifi"]["mdns"].is<bool>() && !config["wifi"]["mdns"].as<bool>()) {
    log(INFO, "🏷️ mDNS: Disabled by configuration");
    return;
  }

  // Get hostname and device name from config
  std::string hostname = getHostname();
  std::string deviceName = config["deviceName"].as<std::string>();

  if (hostname.empty()) {
    log(WARNING, "🏷️ mDNS: Hostname is empty, cannot start mDNS");
    return;
  }

  // Initialize mDNS service
  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    // Log but don't abort - mDNS is non-critical for core functionality
    log(ERROR, "🏷️ mDNS: Failed to initialize: %s", esp_err_to_name(err));
    return;
  }

  // Set hostname for mDNS responder
  err = mdns_hostname_set(hostname.c_str());
  if (err != ESP_OK) {
    log(ERROR, "🏷️ mDNS: Failed to set hostname: %s", esp_err_to_name(err));
    mdns_free(); // Clean up on critical failure
    return;
  }

  // Set default instance name (friendly name shown in mDNS browsers)
  err = mdns_instance_name_set(deviceName.c_str());
  if (err != ESP_OK) {
    log(WARNING, "🏷️ mDNS: Failed to set instance name: %s",
        esp_err_to_name(err));
    // Non-critical, continue
  }

  // Advertise HTTP service on port 80
  err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
  if (err != ESP_OK) {
    log(WARNING, "🏷️ mDNS: Failed to advertise HTTP service: %s",
        esp_err_to_name(err));
    // Non-critical, continue
  } else {
    // Add service text records for better discoverability
    // Note: Using stack variables here is safe as mdns_service_txt_set
    // copies the data internally
    mdns_txt_item_t httpTxt[] = {
        {"board", "ESP32"}, {"version", version().c_str()}, {"path", "/"}};

    err = mdns_service_txt_set("_http", "_tcp", httpTxt,
                               sizeof(httpTxt) / sizeof(httpTxt[0]));
    if (err != ESP_OK) {
      log(DEBUG, "🏷️ mDNS: Failed to set HTTP TXT records: %s",
          esp_err_to_name(err));
      // Non-critical
    }
  }

  // Advertise WebSocket service for RSSI streaming
  err = mdns_service_add(nullptr, "_ws", "_tcp", 80, nullptr, 0);
  if (err != ESP_OK) {
    log(DEBUG, "🏷️ mDNS: Failed to advertise WebSocket service: %s",
        esp_err_to_name(err));
    // Non-critical
  } else {
    mdns_txt_item_t wsTxt[] = {{"path", "/ws/rssi"}};
    err = mdns_service_txt_set("_ws", "_tcp", wsTxt,
                               sizeof(wsTxt) / sizeof(wsTxt[0]));
    if (err != ESP_OK) {
      log(DEBUG, "🏷️ mDNS: Failed to set WebSocket TXT records: %s",
          esp_err_to_name(err));
    }
  }

  // Advertise Arduino/ESP32 service for IDE discovery (OTA updates)
  err = mdns_service_add(nullptr, "_arduino", "_tcp", 3232, nullptr, 0);
  if (err != ESP_OK) {
    log(DEBUG, "🏷️ mDNS: Failed to advertise Arduino service: %s",
        esp_err_to_name(err));
    // Non-critical
  } else {
    const char *boardType = "esp32";
    mdns_txt_item_t arduinoTxt[] = {{"board", boardType},
                                    {"tcp_check", "no"},
                                    {"ssh_upload", "no"},
                                    {"auth_upload", "no"}};
    err = mdns_service_txt_set("_arduino", "_tcp", arduinoTxt,
                               sizeof(arduinoTxt) / sizeof(arduinoTxt[0]));
    if (err != ESP_OK) {
      log(DEBUG, "🏷️ mDNS: Failed to set Arduino TXT records: %s",
          esp_err_to_name(err));
    }
  }

  log(INFO, "🏷️ mDNS: Started successfully");
  log(DEBUG, "🏷️\tHostname: %s.local", hostname.c_str());
  log(DEBUG, "🏷️\tInstance: %s", deviceName.c_str());
  log(DEBUG, "🏷️\tServices: HTTP (80), WebSocket (80), Arduino OTA (3232)");
}

#endif // ESPWiFi_MDNS