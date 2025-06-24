#ifndef ESPWIFI_CONFIG_H
#define ESPWIFI_CONFIG_H

#include "ESPWiFi.h"

void ESPWiFi::saveConfig() {
  File file = LittleFS.open(configFile, "w");
  if (!file) {
    Serial.println("❌  Failed to open config file for writing");
    return;
  }
  size_t written = serializeJson(config, file);
  file.close();
  if (written == 0) {
    Serial.println("❌  Failed to write config JSON to file");
    return;
  }
  Serial.println("💾 Config Saved: " + configFile);
  serializeJsonPretty(config, Serial);
  Serial.println("\n");
}

void ESPWiFi::readConfig() {
  Serial.println("\n⚙️  Reading Config: " + configFile);
  File file = LittleFS.open(configFile, "r");
  if (!file) {
    Serial.println("⚠️  Failed to open config file");
    defaultConfig();
    return;
  }

  DeserializationError error = deserializeJson(config, file);
  if (error) {
    Serial.println("⚠️  Failed to read config file: " + String(error.c_str()));
    defaultConfig();
    file.close();
    return;
  }

  serializeJsonPretty(config, Serial);
  Serial.println("\n");

  file.close();
}

void ESPWiFi::defaultConfig() {
  Serial.println("🛠️  Using Default Config:");
  config["mode"] = "accessPoint";
#ifdef ESP8266
  config["ap"]["ssid"] = "ESPWiFi-" + String(WiFi.hostname());
#else
  config["ap"]["ssid"] = "ESPWiFi " + String(WiFi.getHostname());
#endif
  config["ap"]["password"] = "abcd1234";
  config["mdns"] = "ESPWiFi";
  config["client"]["ssid"] = "";
  config["client"]["password"] = "";

  serializeJsonPretty(config, Serial);
  Serial.println("\n");
}

#endif  // ESPWIFI_CONFIG_H