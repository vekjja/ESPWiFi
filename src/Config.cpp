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

  startLittleFS();

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

  log("⚙️  Config Loaded:");
  logf("\tFile: %s\n", configFile.c_str());

  printConfig();
  file.close();
}

void ESPWiFi::printConfig() {
  String prettyConfig;
  serializeJsonPretty(config, prettyConfig);
  log(prettyConfig);
  // log("\n");
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

  // Camera settings
  config["camera"]["enabled"] = false;
  config["camera"]["frameRate"] = 10;

  // RSSI settings
  config["rssi"]["enabled"] = false;
  config["rssi"]["displayMode"] = "numbers"; // "icon", "numbers", "both"

  // XIAO ESP32S3 Sense PDM Microphone settings
  config["microphone"]["enabled"] = false;
  config["microphone"]["sampleRate"] = 16000; // 8000, 16000, 44100
  config["microphone"]["gain"] = 1.0;         // 0.1 to 10.0
  config["microphone"]["autoGain"] = true;
  config["microphone"]["noiseReduction"] = true;

  printConfig();
}

void ESPWiFi::handleConfig() {
  cameraConfigHandler();
  rssiConfigHandler();
}

#endif // ESPWiFi_CONFIG