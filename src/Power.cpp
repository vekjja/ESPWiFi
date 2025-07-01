#ifndef ESPWIFI_POWER
#define ESPWIFI_POWER

#include "ESPWiFi.h"

#ifdef ESP32
#include <esp_pm.h>
#include <esp_wifi.h>
#endif

void ESPWiFi::applyPowerSettings() {
  // Validate power mode configuration
  String powerMode = "fast";  // Default value
  if (config["power"]["mode"].is<String>()) {
    String modeStr = config["power"]["mode"].as<String>();
    modeStr.toLowerCase();
    if (modeStr == "full" || modeStr == "balanced" || modeStr == "saving") {
      powerMode = modeStr;
    } else {
      log("⚠️  Invalid power mode config value, defaulting to full");
    }
  } else {
    log("⚠️  Missing or invalid power mode config, defaulting to full");
  }

  // Validate wifiSleep configuration
  bool wifiSleep = false;
  if (config["power"]["wifiSleep"].is<bool>()) {
    wifiSleep = config["power"]["wifiSleep"].as<bool>();
  } else if (config["power"]["wifiSleep"].is<int>()) {
    // Handle integer values (0 = false, non-zero = true)
    wifiSleep = config["power"]["wifiSleep"].as<int>() != 0;
  } else if (config["power"]["wifiSleep"].is<String>()) {
    // Handle string values ("true", "false", "1", "0")
    String wifiSleepStr = config["power"]["wifiSleep"].as<String>();
    wifiSleepStr.toLowerCase();
    wifiSleep = (wifiSleepStr == "true" || wifiSleepStr == "1" ||
                 wifiSleepStr == "yes");
  } else {
    // Default to false if invalid or missing
    wifiSleep = false;
    log("⚠️  Invalid wifiSleep config value, defaulting to false");
  }

  log("⚡️ Applying Power Settings:");
  logf("\tMode: %s\n", powerMode.c_str());
  logf("\tWiFi Sleep: %s\n", wifiSleep ? "Enabled" : "Disabled");

  if (powerMode == "full") {
#ifdef ESP32
    WiFi.setTxPower(
        WIFI_POWER_8_5dBm);  // Maximum power for fastest performance
#endif
  } else if (powerMode == "saving") {
#ifdef ESP32
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
#endif
  }
  WiFi.setSleep(wifiSleep);
}

#endif  // ESPWIFI_POWER
