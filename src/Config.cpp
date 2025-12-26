#ifndef ESPWiFi_CONFIG
#define ESPWiFi_CONFIG

#include "ESPWiFi.h"
#include <sys/stat.h>

void ESPWiFi::readConfig() {
  static bool readingConfig = false;
  if (readingConfig) {
    return; // Prevent recursive calls
  }
  readingConfig = true;

  initLittleFS();

  if (!lfs || !littleFsInitialized) {
    config = defaultConfig();
    log(WARNING, "⚙️ No filesystem: Using default config");
    printConfig();
    readingConfig = false;
    return;
  }

  File file = lfs->open(configFile, "r");

  if (!file) {
    config = defaultConfig();
    log(WARNING, "⚙️ Failed to open config file: Using default config");
  } else {
    JsonDocument loadedConfig;
    DeserializationError error = deserializeJson(loadedConfig, file);
    file.close();

    if (error) {
      config = defaultConfig();
      log(WARNING, "⚙️ Failed to read config file: %s: Using default config",
          error.c_str());
    } else {
      mergeConfig(loadedConfig);
    }
  }

  // printConfig();

  readingConfig = false;
}

void ESPWiFi::printConfig() {
  JsonDocument logConfig = config;
  logConfig["wifi"]["accessPoint"]["password"] = "********";
  logConfig["wifi"]["client"]["password"] = "********";
  logConfig["auth"]["password"] = "********";
  std::string prettyConfig;
  serializeJsonPretty(logConfig, prettyConfig);
  log(INFO, "⚙️  Config: " + configFile);
  log(DEBUG, "\n" + prettyConfig);
}

void ESPWiFi::saveConfig() {
  initLittleFS();

  if (!lfs) {
    log(ERROR, "No filesystem available for saving config");
    return;
  }

  File file = lfs->open(configFile, "w");
  if (!file) {
    log(ERROR, " Failed to open config file for writing");
    return;
  }

  std::string jsonStr;
  size_t size = measureJson(config);
  jsonStr.reserve(size + 1);

  // Serialize to string
  char *buffer = (char *)malloc(size + 1);
  if (buffer) {
    size_t written = serializeJson(config, buffer, size + 1);
    file.write((const uint8_t *)buffer, written);
    free(buffer);
    file.close();

    if (written == 0) {
      log(ERROR, " Failed to write config JSON to file");
      return;
    }

    if (config["log"]["enabled"].as<bool>()) {
      log(INFO, "💾 Config Saved: %s", configFile.c_str());
      printConfig();
    }
  } else {
    file.close();
    log(ERROR, " Failed to allocate memory for config");
  }
}

void ESPWiFi::mergeConfig(JsonDocument &json) {
  // Deep merge: recursively merge objects instead of replacing them
  std::function<void(JsonVariant, JsonVariantConst)> deepMerge =
      [&](JsonVariant dst, JsonVariantConst src) {
        if (src.is<JsonObjectConst>() && dst.is<JsonObject>()) {
          // Both are objects - merge recursively
          for (JsonPairConst kvp : src.as<JsonObjectConst>()) {
            JsonVariant dstValue = dst[kvp.key()];
            if (dstValue.isNull()) {
              // Key doesn't exist in destination, just copy it
              dst[kvp.key()] = kvp.value();
            } else {
              // Key exists, recurse to merge deeper
              deepMerge(dstValue, kvp.value());
            }
          }
        } else {
          // Not both objects (or one is null), just replace the value
          dst.set(src);
        }
      };

  // Start the deep merge from root
  deepMerge(config.as<JsonVariant>(), json.as<JsonVariantConst>());
}

void ESPWiFi::handleConfig() {
  bluetoothConfigHandler();
#ifdef ESPWiFi_CAMERA
  cameraConfigHandler();
#else
  config["camera"]["enabled"] = false;
  config["camera"]["installed"] = false;
#endif
  logConfigHandler();
}

JsonDocument ESPWiFi::defaultConfig() {
  JsonDocument defaultConfig;

  std::string hostname = "espwifi32-123456";
  defaultConfig["hostname"] = hostname; // Will get MAC-based hostname later
  defaultConfig["deviceName"] = "ESPWiFi";

  defaultConfig["wifi"]["enabled"] = true;
  defaultConfig["wifi"]["mode"] = "accessPoint";

  // Access Point
  std::string ssid = "ESPWiFi-" + hostname;
  defaultConfig["wifi"]["accessPoint"]["ssid"] = ssid;
  defaultConfig["wifi"]["accessPoint"]["password"] = "espwifi!";

  // WiFi Client
  defaultConfig["wifi"]["client"]["ssid"] = "";
  defaultConfig["wifi"]["client"]["password"] = "";

// Bluetooth (BLE - works on ESP32-S3 and ESP32-C3)
#if defined(CONFIG_BT_ENABLED)
  defaultConfig["bluetooth"]["installed"] = true;
#else
  defaultConfig["bluetooth"]["installed"] = false;
#endif
  defaultConfig["bluetooth"]["enabled"] = false;

  // Camera
#ifdef ESPWiFi_CAMERA
  defaultConfig["camera"]["installed"] = true;
  defaultConfig["camera"]["enabled"] = false;
  defaultConfig["camera"]["frameRate"] = 10;
  defaultConfig["camera"]["rotation"] = 0;
  defaultConfig["camera"]["brightness"] = 1;
  defaultConfig["camera"]["contrast"] = 1;
  defaultConfig["camera"]["saturation"] = 1;
  defaultConfig["camera"]["exposure_level"] = 1;
  defaultConfig["camera"]["exposure_value"] = 400;
  defaultConfig["camera"]["agc_gain"] = 2;
  defaultConfig["camera"]["gain_ceiling"] = 2;
  defaultConfig["camera"]["white_balance"] = 1;
  defaultConfig["camera"]["awb_gain"] = 1;
  defaultConfig["camera"]["wb_mode"] = 0;
#else
  defaultConfig["camera"]["installed"] = false;
#endif

  // OTA - based on partition table
  defaultConfig["ota"]["enabled"] = isOTAEnabled();

  // Auth
  defaultConfig["auth"]["enabled"] = false;
  defaultConfig["auth"]["password"] = "admin";
  defaultConfig["auth"]["username"] = "admin";

  // Logging: access, debug, info, warning, error
  defaultConfig["log"]["enabled"] = true;
  defaultConfig["log"]["level"] = "debug";

  return defaultConfig;
}

// Commenting out web server config endpoint for now
// void ESPWiFi::srvConfig() {
//   // Will implement with ESP-IDF HTTP server later
// }

#endif // ESPWiFi_CONFIG
