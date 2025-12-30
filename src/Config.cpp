#ifndef ESPWiFi_CONFIG
#define ESPWiFi_CONFIG

#include "ESPWiFi.h"

void ESPWiFi::readConfig() {
  initLittleFS();
  bool usedDefaultConfig = false;

  if (!littleFsInitialized) {
    config = defaultConfig();
    usedDefaultConfig = true;
    log(ERROR, "⚙️ Could not access filesystem: Using default config");
  } else {

    size_t fileSize = 0;
    char *buffer = readFile(configFile, &fileSize);

    if (!buffer || fileSize == 0 || fileSize >= 10240) { // Limit to 10KB
      if (buffer)
        free(buffer);
      config = defaultConfig();
      usedDefaultConfig = true;
      log(WARNING, "⚙️ Failed to read config file: Using default config");
    } else {
      // Parse JSON (be robust to UTF-8 BOM / leading whitespace).
      JsonDocument loadedConfig;

      const char *json = buffer;
      size_t jsonLen = fileSize;

      // Strip UTF-8 BOM if present (0xEF 0xBB 0xBF)
      if (jsonLen >= 3 && (uint8_t)json[0] == 0xEF &&
          (uint8_t)json[1] == 0xBB && (uint8_t)json[2] == 0xBF) {
        json += 3;
        jsonLen -= 3;
      }
      // Strip leading whitespace
      while (jsonLen > 0 && isspace((unsigned char)*json)) {
        json++;
        jsonLen--;
      }

      DeserializationError error = deserializeJson(loadedConfig, json, jsonLen);
      free(buffer);

      if (error) {
        config = defaultConfig();
        usedDefaultConfig = true;
        log(WARNING,
            "⚙️ Failed to parse config file (%s, %u bytes): %s: Using default "
            "config",
            configFile.c_str(), (unsigned)fileSize, error.c_str());

        // Best-effort persist defaults so the next boot is clean.
        saveConfig(config);
      } else {
        config = mergeJson(config, loadedConfig);
      }
    }
  }

  if (usedDefaultConfig) {
    log(WARNING, "⚙️ Using Default Config:\n%s", prettyConfig());
  } else {
    log(INFO, "⚙️ Using Config:\n%s", prettyConfig());
  }
}

std::string ESPWiFi::prettyConfig() {
  // Create a copy of config with masked passwords for logging
  JsonDocument logConfig;
  std::string configJson;
  serializeJson(config, configJson);
  yield(); // Yield after serialization
  DeserializationError error = deserializeJson(logConfig, configJson);
  if (error) {
    log(WARNING, "⚙️ Failed to deserialize config for printing: %s",
        error.c_str());
    return "";
  }

  // Recursively mask any fields with keys matching sensitive field names
  const char *sensitiveKeys[] = {"password", "passwd",  "key",   "token",
                                 "apiKey",   "api_key", "secret"};
  const size_t numSensitiveKeys =
      sizeof(sensitiveKeys) / sizeof(sensitiveKeys[0]);

  // const int MAX_DEPTH = 10; // Limit recursion depth to prevent stack
  // overflow
  std::function<void(JsonVariant, int)> maskSensitiveFields =
      [&](JsonVariant variant, int depth) {
        // if (depth > MAX_DEPTH) {
        //   return; // Too deep, skip masking
        // }

        if (variant.is<JsonObject>()) {
          JsonObject obj = variant.as<JsonObject>();
          for (JsonPair kvp : obj) {
            std::string key = kvp.key().c_str();
            JsonVariant value = obj[key]; // Get mutable reference

            // Case-insensitive check for sensitive fields
            std::string lowerKey = key;
            for (char &c : lowerKey) {
              c = tolower(c);
            }

            bool shouldMask = false;
            for (size_t i = 0; i < numSensitiveKeys; i++) {
              if (lowerKey == sensitiveKeys[i]) {
                shouldMask = true;
                break;
              }
            }

            if (shouldMask) {
              value.set("********");
            } else if (value.is<JsonObject>() || value.is<JsonArray>()) {
              // Recursively process nested objects and arrays
              maskSensitiveFields(value, depth + 1);
            }

            // Yield periodically to prevent stack overflow
            if (depth % 3 == 0) {
              yield();
            }
          }
        } else if (variant.is<JsonArray>()) {
          JsonArray arr = variant.as<JsonArray>();
          for (JsonVariant item : arr) {
            if (item.is<JsonObject>() || item.is<JsonArray>()) {
              maskSensitiveFields(item, depth + 1);
            }
          }
        }
      };

  maskSensitiveFields(logConfig.as<JsonVariant>(), 0);

  std::string prettyConfig;
  serializeJsonPretty(logConfig, prettyConfig);
  return prettyConfig;
}

void ESPWiFi::saveConfig(JsonDocument &configToSave) {
  initLittleFS();

  if (!littleFsInitialized) {
    log(ERROR, "⚙️ No filesystem available for saving config");
    return;
  }

  yield(); // Yield before JSON operations

  // Serialize to buffer - add extra padding for safety
  size_t size = measureJson(configToSave);
  if (size == 0) {
    log(ERROR, "⚙️ Failed to measure config JSON size");
    return;
  }

  // Allocate buffer with extra space for safety
  char *buffer = (char *)malloc(size + 32);
  if (!buffer) {
    log(ERROR, "⚙️ Failed to allocate memory for config (size: %zu)", size);
    return;
  }

  yield(); // Yield after allocation

  size_t written = serializeJson(configToSave, buffer, size + 32);
  if (written == 0) {
    log(ERROR, "⚙️ Failed to serialize config JSON");
    free(buffer);
    return;
  }

  yield(); // Yield after serialization

  // Use atomic write to prevent corruption
  bool success = writeFile(configFile, (const uint8_t *)buffer, written);
  free(buffer);

  if (!success) {
    log(ERROR, "⚙️ Failed to write config file");
    return;
  }

  yield(); // Yield after file write

  if (configToSave["log"]["enabled"].as<bool>()) {
    log(INFO, "⚙️ Config Saved: %s", configFile);
    // prettyConfig();
  }
}

void ESPWiFi::saveConfig() {
  initLittleFS();

  if (!littleFsInitialized) {
    log(ERROR, "⚙️ No filesystem available for saving config");
    return;
  }

  // Serialize to buffer
  size_t size = measureJson(config);
  char *buffer = (char *)malloc(size + 1);
  if (!buffer) {
    log(ERROR, "⚙️ Failed to allocate memory for config");
    return;
  }

  size_t written = serializeJson(config, buffer, size + 1);

  // Use atomic write to prevent corruption
  bool success = writeFile(configFile, (const uint8_t *)buffer, written);
  free(buffer);

  if (!success) {
    log(ERROR, "⚙️ Failed to write config file");
    return;
  }

  if (config["log"]["enabled"].as<bool>()) {
    log(INFO, "⚙️ Config Saved: %s", configFile);
  }
}

JsonDocument ESPWiFi::mergeJson(const JsonDocument &base,
                                const JsonDocument &updates) {
  // Create a new JsonDocument by copying the base
  JsonDocument result;

  // Copy base into result by serializing and deserializing
  std::string baseJson;
  serializeJson(base, baseJson);
  DeserializationError error = deserializeJson(result, baseJson);
  if (error) {
    // If copy fails, return empty document
    log(ERROR, "⚙️ Failed to copy base JSON in mergeJson: %s", error.c_str());
    return result;
  }

  // Deep merge: implemented as an ESPWiFi member in Utils.cpp so all services
  // share one implementation.
  deepMerge(result.as<JsonVariant>(), updates.as<JsonVariantConst>(), 0);

  return result;
}

void ESPWiFi::requestConfigUpdate() {
  // Single “commit” flag: apply config + save config in main task.
  configNeedsUpdate = true;
}

void ESPWiFi::handleConfig() {
  refreshCorsCache();
  logConfigHandler();
  sdCardConfigHandler();
  bluetoothConfigHandler();
}

JsonDocument ESPWiFi::defaultConfig() {
  JsonDocument doc;

  doc["deviceName"] = "ESPWiFi";
  doc["hostname"] = getHostname();

  doc["wifi"]["enabled"] = true;
  doc["wifi"]["mode"] = "accessPoint";

  // Access Point
  doc["wifi"]["accessPoint"]["ssid"] = doc["deviceName"].as<std::string>();
  doc["wifi"]["accessPoint"]["password"] = "espwifi!";

  // WiFi Client
  doc["wifi"]["client"]["ssid"] = "";
  doc["wifi"]["client"]["password"] = "";

  // Bluetooth (BLE - works on ESP32-S3 and ESP32-C3)
  doc["bluetooth"]["enabled"] = false;

  // SD Card
  doc["sd"]["enabled"] = true;
  doc["sd"]["initialized"] = false;
  doc["sd"]["type"] = "auto";

  // Logging: verbose, access, debug, info, warning, error
  doc["log"]["file"] = "/log";
  doc["log"]["enabled"] = true;
  doc["log"]["level"] = "debug";
  doc["log"]["preferSD"] = true;

  // Auth
  doc["auth"]["enabled"] = false;
  doc["auth"]["password"] = "admin";
  doc["auth"]["username"] = "admin";
  // CORS (auth.cors)
  // - enabled: controls whether CORS headers are emitted
  // - origins: allowed Origin patterns (supports '*' and '?')
  // - methods: allowed methods for preflight
  // - paths: optional URI path patterns to apply CORS to (supports '*' and '?')
  doc["auth"]["cors"]["enabled"] = true;
  JsonArray corsOrigins = doc["auth"]["cors"]["origins"].to<JsonArray>();
  corsOrigins.add("*");
  JsonArray corsMethods = doc["auth"]["cors"]["methods"].to<JsonArray>();
  corsMethods.add("GET");
  corsMethods.add("POST");
  corsMethods.add("PUT");
  corsMethods.add("DELETE");
  JsonArray excludePaths = doc["auth"]["excludePaths"].to<JsonArray>();
  excludePaths.add("/");
  excludePaths.add("/static/*");
  excludePaths.add("/favicon.ico");
  excludePaths.add("/api/auth/login");
  excludePaths.add("/asset-manifest.json");

  // Paths that are always protected from HTTP file APIs (even when authorized).
  // Patterns support '*' and '?' (same matcher as excludePaths).
  JsonArray protectFiles = doc["auth"]["protectFiles"].to<JsonArray>();
  protectFiles.add("/log");
  protectFiles.add("/static/*");
  protectFiles.add("/index.html");
  protectFiles.add("/config.json");
  protectFiles.add("/asset-manifest.json");

  // Camera
  // #ifdef ESPWiFi_CAMERA
  //   doc["camera"]["installed"] = true;
  //   doc["camera"]["enabled"] = false;
  //   doc["camera"]["frameRate"] = 10;
  //   doc["camera"]["rotation"] = 0;
  //   doc["camera"]["brightness"] = 1;
  //   doc["camera"]["contrast"] = 1;
  //   doc["camera"]["saturation"] = 1;
  //   doc["camera"]["exposure_level"] = 1;
  //   doc["camera"]["exposure_value"] = 400;
  //   doc["camera"]["agc_gain"] = 2;
  //   doc["camera"]["gain_ceiling"] = 2;
  //   doc["camera"]["white_balance"] = 1;
  //   doc["camera"]["awb_gain"] = 1;
  //   doc["camera"]["wb_mode"] = 0;
  // #else
  //   doc["camera"]["installed"] = false;
  // #endif

  // OTA - based on LittleFS partition
  doc["ota"]["enabled"] = isOTAEnabled();

  return doc;
}

#endif // ESPWiFi_CONFIG
