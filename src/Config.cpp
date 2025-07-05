#ifndef ESPWiFi_CONFIG
#define ESPWiFi_CONFIG

#include "ESPWiFi.h"

void ESPWiFi::saveConfig() {
  startLittleFS();
  File file = LittleFS.open(configFile, "w");
  if (!file) {
    logError(" Failed to open config file for writing");
    return;
  }
  size_t written = serializeJson(config, file);
  file.close();
  if (written == 0) {
    logError(" Failed to write config JSON to file");
    return;
  }
  log("💾 Config Saved: " + configFile);
  printConfig();
}

void ESPWiFi::readConfig() {
  if (config["mdns"].is<String>()) {
    return;
  }

  startLittleFS();
  log("⚙️  Config Loading:");
  logf("\tFile: %s\n", configFile.c_str());

  File file = LittleFS.open(configFile, "r");
  if (!file) {
    logError("Failed to open config file");
    defaultConfig();
    return;
  }

  DeserializationError error = deserializeJson(config, file);
  if (error) {
    logError("Failed to read config file: " + String(error.c_str()));
    defaultConfig();
    file.close();
    return;
  }

  printConfig();
  file.close();
}

void ESPWiFi::printConfig() {
  String prettyConfig;
  serializeJsonPretty(config, prettyConfig);
  log(prettyConfig);
  log("\n");
}

void ESPWiFi::mergeConfig(JsonObject &json) {
  for (JsonPair kv : json) {
    config[kv.key()] = kv.value();
  }
}

void ESPWiFi::defaultConfig() {
  log("🛠️  Using Default Config:");
  config["mode"] = "accessPoint";
#ifdef ESP8266
  config["ap"]["ssid"] = "ESPWiFi-" + String(WiFi.hostname());
#else
  config["ap"]["ssid"] = "ESPWiFi-" + String(WiFi.getHostname());
#endif
  config["ap"]["password"] = "abcd1234";
  config["mdns"] = "ESPWiFi";
  config["client"]["ssid"] = "";
  config["client"]["password"] = "";

  // Power management settings
  config["power"]["mode"] = "full"; // "full", "balanced", "saving"
  config["power"]["wifiSleep"] = false;

  printConfig();
}

#endif // ESPWiFi_CONFIG