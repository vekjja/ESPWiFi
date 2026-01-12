#ifndef ESPWiFi_CONFIG
#define ESPWiFi_CONFIG

#include "ESPWiFi.h"

void ESPWiFi::readConfig() {

  // Try to read config file and override defaults
  initLittleFS();
  bool configLoaded = false;

  if (lfs != nullptr) {
    size_t fileSize = 0;
    char *buffer = readFile(configFile, &fileSize);

    if (buffer && fileSize > 0 && fileSize < 10240) { // Limit to 10KB
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

      if (!error) {
        config = mergeJson(config, loadedConfig);
        configLoaded = true;
      } else {
        log(WARNING,
            "⚙️ Failed to parse config file (%s, %u bytes): %s: Using default "
            "config",
            configFile.c_str(), (unsigned)fileSize, error.c_str());
        // Best-effort persist defaults so the next boot is clean.
        saveConfig();
      }
    } else {
      if (buffer)
        free(buffer);
      if (fileSize > 0) {
        log(WARNING, "⚙️ Failed to read config file: Using default config");
      }
    }
  } else {
    log(ERROR, "⚙️ Could not access filesystem: Using default config");
  }

  corsConfigHandler();
  if (configLoaded) {
    log(INFO, "⚙️ Config Read from LittleFS (size: %zu bytes)",
        measureJson(config));
  } else {
    log(WARNING, "⚙️ Using Default Config");
  }
}

void ESPWiFi::maskSensitiveFields(JsonVariant variant) {
  if (variant.is<JsonObject>()) {
    JsonArray sensitiveKeys = config["log"]["maskedKeys"].as<JsonArray>();
    JsonObject obj = variant.as<JsonObject>();
    for (JsonPair kvp : obj) {
      const char *key = kvp.key().c_str();
      JsonVariant value = obj[key];

      // Check if this key should be masked
      bool shouldMask = false;
      for (JsonVariant maskedKey : sensitiveKeys) {
        if (strcmp(key, maskedKey.as<const char *>()) == 0) {
          shouldMask = true;
          break;
        }
      }

      if (shouldMask) {
        value.set("********");
      } else if (value.is<JsonObject>() || value.is<JsonArray>()) {
        // Recursively process nested objects and arrays
        maskSensitiveFields(value);
      }
    }
  } else if (variant.is<JsonArray>()) {
    JsonArray arr = variant.as<JsonArray>();
    for (JsonVariant item : arr) {
      if (item.is<JsonObject>() || item.is<JsonArray>()) {
        maskSensitiveFields(item);
      }
    }
  }
}

std::string ESPWiFi::prettyConfig() {
  // Optimized: work directly on config copy to avoid multiple large string
  // allocations Use heap-allocated buffer for serialization to reduce stack
  // pressure
  size_t size = measureJson(config);
  if (size == 0) {
    return "";
  }

  // Allocate buffer on heap instead of stack
  char *buffer = (char *)malloc(size + 32);
  if (!buffer) {
    return "";
  }

  size_t written = serializeJson(config, buffer, size + 32);
  if (written == 0) {
    free(buffer);
    return "";
  }

  feedWatchDog(); // Yield after serialization

  // Parse into temporary document for masking
  JsonDocument logConfig;
  DeserializationError error = deserializeJson(logConfig, buffer, written);
  free(buffer); // Free immediately after parsing

  if (error) {
    log(WARNING, "⚙️ Failed to deserialize config for printing: %s",
        error.c_str());
    return "";
  }

  // Recursively mask sensitive fields at all depths
  maskSensitiveFields(logConfig.as<JsonVariant>());

  // Serialize to string (this is the only large string we create)
  std::string prettyConfig;
  prettyConfig.reserve(size + 100); // Pre-allocate to reduce reallocations
  serializeJsonPretty(logConfig, prettyConfig);
  return prettyConfig;
}

void ESPWiFi::saveConfig() {
  initLittleFS();

  if (lfs == nullptr) {
    log(ERROR, "⚙️ No filesystem available for saving config");
    return;
  }

  feedWatchDog(); // Yield before JSON operations

  // Serialize to buffer - add extra padding for safety
  size_t size = measureJson(config);
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

  feedWatchDog(); // Yield after allocation

  size_t written = serializeJson(config, buffer, size + 32);
  if (written == 0) {
    log(ERROR, "⚙️ Failed to serialize config JSON");
    free(buffer);
    return;
  }

  feedWatchDog(); // Yield after serialization

  bool success = writeFile(configFile, (const uint8_t *)buffer, written);
  free(buffer);

  if (!success) {
    log(ERROR, "⚙️ Failed to write config file");
    return;
  }

  feedWatchDog(); // Yield after file write

  if (config["log"]["enabled"].as<bool>()) {
    log(INFO, "⚙️ Config Saved: %s", configFile);
  }
  // log(DEBUG, "⚙️\n%s", prettyConfig());

  configNeedsSave = false;
}

JsonDocument ESPWiFi::mergeJson(const JsonDocument &base,
                                const JsonDocument &updates) {
  // Create a new JsonDocument by copying the base
  JsonDocument result;

  // Copy base into result by serializing and deserializing
  // Use heap buffer to avoid large stack allocation
  size_t size = measureJson(base);
  if (size == 0) {
    log(ERROR, "⚙️ Failed to measure base JSON size in mergeJson");
    return result;
  }

  // Allocate buffer on heap instead of stack to prevent overflow
  char *buffer = (char *)malloc(size + 32); // +32 is extra padding for safety
  if (!buffer) {
    log(ERROR, "⚙️ Failed to allocate memory for mergeJson (size: %zu)", size);
    return result;
  }

  size_t written = serializeJson(base, buffer, size + 32);
  if (written == 0) {
    free(buffer);
    log(ERROR, "⚙️ Failed to serialize base JSON in mergeJson");
    return result;
  }

  DeserializationError error = deserializeJson(result, buffer, written);
  free(buffer);

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

void ESPWiFi::requestConfigSave() { configNeedsSave = true; }

bool ESPWiFi::queueConfigUpdate(JsonVariantConst updates) {
  if (updates.isNull()) {
    return false;
  }
  JsonDocument upd;
  upd.set(updates);
  configUpdate = mergeJson(config, upd);
  requestConfigSave();
  return configUpdate.size() > 0;
}

void ESPWiFi::handleConfigUpdate() {

  if (configUpdate.size() > 0) {
    // Save old config for comparison in handlers
    oldConfig = config;
    // Apply new config
    config = configUpdate;

    cameraConfigHandler();
    powerConfigHandler();
    corsConfigHandler();
    logConfigHandler();
    bleConfigHandler();
    bluetoothConfigHandler();
    wifiConfigHandler();
  }

  configUpdate.clear();
  if (configNeedsSave) {
    saveConfig();
  }
}

#endif // ESPWiFi_CONFIG
