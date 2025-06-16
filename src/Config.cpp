#ifndef ESPWIFI_CONFIG_H
#define ESPWIFI_CONFIG_H

#include "ESPWiFi.h"

void ESPWiFi::saveConfig() {
  File file = LittleFS.open(configFile, "w");
  if (!file) {
    Serial.println("⚠️  Failed to open config file for writing");
    return;
  }
  serializeJson(config, file);
  file.close();
  Serial.println("💾 Config saved to " + configFile);
  serializeJsonPretty(config, Serial);
  Serial.println("\n");
}

void ESPWiFi::readConfig() {
  Serial.println("\n🛠️  Reading config file: " + configFile);
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
  config["mode"] = "ap";
#ifdef ESP8266
  config["ap"]["ssid"] = "ESPWiFi-" + String(ESP.getChipId(), HEX);
#else
  config["ap"]["ssid"] = "ESPWiFi-" + String(ESP.getEfuseMac(), HEX);
#endif
  config["ap"]["password"] = "abcd1234";
  config["mdns"] = "ESPWiFi";
  config["client"]["ssid"] = "";
  config["client"]["password"] = "";

  serializeJsonPretty(config, Serial);
  Serial.println("\n");
}

#endif  // ESPWIFI_CONFIG_H