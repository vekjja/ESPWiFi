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

  if (!littleFsInitialized) {
    config = defaultConfig();
    log(WARNING, "⚙️ No filesystem: Using default config");
    printConfig();
    readingConfig = false;
    return;
  }

  size_t fileSize = 0;
  char *buffer = readFile(configFile, &fileSize);

  if (!buffer || fileSize == 0 || fileSize >= 10240) { // Limit to 10KB
    if (buffer)
      free(buffer);
    config = defaultConfig();
    log(WARNING, "⚙️ Failed to read config file: Using default config");
  } else {
    // Parse JSON
    JsonDocument loadedConfig;
    DeserializationError error = deserializeJson(loadedConfig, buffer);
    free(buffer);

    if (error) {
      config = defaultConfig();
      log(WARNING, "⚙️ Failed to parse config file: %s: Using default config",
          error.c_str());
    } else {
      config = mergeJson(config, loadedConfig);
    }
  }

  log(INFO, "⚙️  Config loaded: %s", configFile.c_str());
  printConfig();

  readingConfig = false;
}

void ESPWiFi::printConfig() {
  // log(INFO, "⚙️  Config: " + configFile);

  vTaskDelay(pdMS_TO_TICKS(1)); // Yield before JSON operations
  // Create a copy of config with masked passwords for logging
  JsonDocument logConfig;
  std::string configJson;
  serializeJson(config, configJson);
  vTaskDelay(pdMS_TO_TICKS(1)); // Yield after serialization
  DeserializationError error = deserializeJson(logConfig, configJson);
  if (error) {
    // If copy fails, just log the original (passwords will be visible)
    vTaskDelay(pdMS_TO_TICKS(1)); // Yield before pretty serialization
    std::string prettyConfig;
    serializeJsonPretty(config, prettyConfig);
    vTaskDelay(pdMS_TO_TICKS(1)); // Yield after pretty serialization
    log(DEBUG, "\n" + prettyConfig);
    return;
  }

  // Recursively mask any fields with keys matching sensitive field names
  // ArduinoJson doesn't have a built-in recursive search, so we implement our
  // own
  const char *sensitiveKeys[] = {"password", "passwd",  "key",   "token",
                                 "apiKey",   "api_key", "secret"};
  const size_t numSensitiveKeys =
      sizeof(sensitiveKeys) / sizeof(sensitiveKeys[0]);

  const int MAX_DEPTH = 10; // Limit recursion depth to prevent stack overflow
  std::function<void(JsonVariant, int)> maskSensitiveFields =
      [&](JsonVariant variant, int depth) {
        if (depth > MAX_DEPTH) {
          return; // Too deep, skip masking
        }

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
              vTaskDelay(pdMS_TO_TICKS(1));
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
  vTaskDelay(pdMS_TO_TICKS(1)); // Yield after masking

  std::string prettyConfig;
  serializeJsonPretty(logConfig, prettyConfig);
  vTaskDelay(pdMS_TO_TICKS(1)); // Yield after pretty serialization
  log(DEBUG, "\n" + prettyConfig);
}

void ESPWiFi::saveConfig(JsonDocument &configToSave) {
  initLittleFS();

  if (!littleFsInitialized) {
    log(ERROR, "No filesystem available for saving config");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(1)); // Yield before JSON operations

  // Serialize to buffer - add extra padding for safety
  size_t size = measureJson(configToSave);
  if (size == 0) {
    log(ERROR, "Failed to measure config JSON size");
    return;
  }

  // Allocate buffer with extra space for safety
  char *buffer = (char *)malloc(size + 32);
  if (!buffer) {
    log(ERROR, " Failed to allocate memory for config (size: %zu)", size);
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(1)); // Yield after allocation

  size_t written = serializeJson(configToSave, buffer, size + 32);
  if (written == 0) {
    log(ERROR, "Failed to serialize config JSON");
    free(buffer);
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(1)); // Yield after serialization

  // Use atomic write to prevent corruption
  bool success = writeFileAtomic(configFile, (const uint8_t *)buffer, written);
  free(buffer);

  if (!success) {
    log(ERROR, "💾  Failed to write config file");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(1)); // Yield after file write

  if (configToSave["log"]["enabled"].as<bool>()) {
    log(INFO, "💾  Config Saved: %s", configFile.c_str());
    // printConfig();
  }
}

void ESPWiFi::saveConfig() {
  initLittleFS();

  if (!littleFsInitialized) {
    log(ERROR, "No filesystem available for saving config");
    return;
  }

  // Serialize to buffer
  size_t size = measureJson(config);
  char *buffer = (char *)malloc(size + 1);
  if (!buffer) {
    log(ERROR, " Failed to allocate memory for config");
    return;
  }

  size_t written = serializeJson(config, buffer, size + 1);

  // Use atomic write to prevent corruption
  bool success = writeFileAtomic(configFile, (const uint8_t *)buffer, written);
  free(buffer);

  if (!success) {
    log(ERROR, " Failed to write config file");
    return;
  }

  if (config["log"]["enabled"].as<bool>()) {
    log(INFO, "💾 Config Saved: %s", configFile.c_str());
    printConfig();
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
    log(ERROR, "Failed to copy base JSON in mergeJson: %s", error.c_str());
    return result;
  }

  // Deep merge: recursively merge objects instead of replacing them
  // Limit recursion depth to prevent stack overflow
  const int MAX_DEPTH = 10;
  std::function<void(JsonVariant, JsonVariantConst, int)> deepMerge =
      [&](JsonVariant dst, JsonVariantConst src, int depth) {
        if (depth > MAX_DEPTH) {
          // Too deep, just replace instead of merging
          dst.set(src);
          return;
        }

        if (src.is<JsonObjectConst>() && dst.is<JsonObject>()) {
          // Both are objects - merge recursively
          for (JsonPairConst kvp : src.as<JsonObjectConst>()) {
            JsonVariant dstValue = dst[kvp.key()];
            if (dstValue.isNull()) {
              // Key doesn't exist in destination, just copy it
              dst[kvp.key()] = kvp.value();
            } else {
              // Key exists, recurse to merge deeper
              deepMerge(dstValue, kvp.value(), depth + 1);
            }
          }
          // Yield periodically during merge to prevent stack overflow
          if (depth % 3 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        } else {
          // Not both objects (or one is null), just replace the value
          dst.set(src);
        }
      };

  // Start the deep merge from root
  deepMerge(result.as<JsonVariant>(), updates.as<JsonVariantConst>(), 0);

  return result;
}

void ESPWiFi::requestConfigSave() {
  // Set flag to save config from main task (safe for filesystem operations)
  configNeedsSave = true;
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
#if defined(CONFIG_BT_ENABLED)
  doc["bluetooth"]["installed"] = true;
#else
  doc["bluetooth"]["installed"] = false;
#endif
  doc["bluetooth"]["enabled"] = false;

  // Camera
#ifdef ESPWiFi_CAMERA
  doc["camera"]["installed"] = true;
  doc["camera"]["enabled"] = false;
  doc["camera"]["frameRate"] = 10;
  doc["camera"]["rotation"] = 0;
  doc["camera"]["brightness"] = 1;
  doc["camera"]["contrast"] = 1;
  doc["camera"]["saturation"] = 1;
  doc["camera"]["exposure_level"] = 1;
  doc["camera"]["exposure_value"] = 400;
  doc["camera"]["agc_gain"] = 2;
  doc["camera"]["gain_ceiling"] = 2;
  doc["camera"]["white_balance"] = 1;
  doc["camera"]["awb_gain"] = 1;
  doc["camera"]["wb_mode"] = 0;
#else
  doc["camera"]["installed"] = false;
#endif

  // OTA - based on LittleFS partition
  doc["ota"]["enabled"] = isOTAEnabled();

  // Auth
  doc["auth"]["enabled"] = false;
  doc["auth"]["password"] = "admin";
  doc["auth"]["username"] = "admin";

  // Logging: access, debug, info, warning, error
  doc["log"]["enabled"] = true;
  doc["log"]["level"] = "debug";

  return doc;
}

void ESPWiFi::srvConfig() {
  if (!webServer) {
    log(ERROR,
        "Cannot start config API /api/config: web server not initialized");
    return;
  }

  // Config GET endpoint
  httpd_uri_t config_get_route = {.uri = "/config",
                                  .method = HTTP_GET,
                                  .handler = [](httpd_req_t *req) -> esp_err_t {
                                    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

                                    std::string json;
                                    serializeJson(espwifi->config, json);
                                    espwifi->sendJsonResponse(req, 200, json);
                                    return ESP_OK;
                                  },
                                  .user_ctx = this};
  httpd_register_uri_handler(webServer, &config_get_route);

  // Config PUT endpoint
  httpd_uri_t config_put_route = {
      .uri = "/config",
      .method = HTTP_PUT,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

        JsonDocument reqJson = espwifi->readRequestBody(req);

        // Check if JSON document is empty (parse failed or empty input)
        if (reqJson.size() == 0) {
          espwifi->sendJsonResponse(req, 400, "{\"error\":\"EmptyInput\"}");
          espwifi->log(ERROR, "/config Error parsing JSON: EmptyInput");
          return ESP_OK;
        }

        JsonDocument mergedConfig =
            espwifi->mergeJson(espwifi->config, reqJson);

        espwifi->config = mergedConfig;

        std::string responseJson;
        serializeJson(mergedConfig, responseJson);

        // Request deferred config save (will happen in runSystem() main task)
        espwifi->requestConfigSave();
        espwifi->handleConfig();

        // Return the updated config (using the already serialized string)
        espwifi->sendJsonResponse(req, 200, responseJson);
        return ESP_OK;
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &config_put_route);
}

#endif // ESPWiFi_CONFIG
