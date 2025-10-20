#ifndef ESPWiFi_CONFIG
#define ESPWiFi_CONFIG

#include "ESPWiFi.h"

void ESPWiFi::readConfig() {

  initLittleFS();

  File file = LittleFS.open(configFile, "r");
  if (!file) {
    log("⚠️  Failed to open config file\nUsing default config");
    config = defaultConfig();
  }

  DeserializationError error = deserializeJson(config, file);
  if (error) {
    log("⚠️  Failed to read config file: " + String(error.c_str()) +
        "\nUsing default config");
    config = defaultConfig();
    file.close();
  }

  log("⚙️  Config Loaded:");
  logf("\tFile: %s\n", configFile.c_str());

  printConfig();
  file.close();
}

void ESPWiFi::saveConfig() {
  initLittleFS();
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

void ESPWiFi::printConfig() {
  String prettyConfig;
  serializeJsonPretty(config, prettyConfig);
  log(prettyConfig);
}

void ESPWiFi::mergeConfig(JsonObject &json) {
  for (JsonPair kv : json) {
    config[kv.key()] = kv.value();
  }
}

JsonDocument ESPWiFi::defaultConfig() {
  JsonDocument defaultConfig;
  String hostname;

#ifdef ESP8266
  hostname = String(WiFi.hostname());
#else
  hostname = String(WiFi.getHostname());
#endif

  defaultConfig["mode"] = "accessPoint";
  defaultConfig["mdns"] = "ESPWiFi";

  defaultConfig["ap"]["ssid"] = "ESPWiFi-" + hostname;
  defaultConfig["ap"]["password"] = "abcd1234";

  defaultConfig["client"]["ssid"] = "";
  defaultConfig["client"]["password"] = "";

  // Camera settings
  defaultConfig["camera"]["enabled"] = false;
  defaultConfig["camera"]["frameRate"] = 10;

  // RSSI settings
  defaultConfig["rssi"]["enabled"] = false;
  defaultConfig["rssi"]["displayMode"] = "numbers";

  return defaultConfig;
}

void ESPWiFi::handleConfig() {
  cameraConfigHandler();
  rssiConfigHandler();
}

#endif // ESPWiFi_CONFIG