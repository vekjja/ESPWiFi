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

  std::string full_path = lfsMountPoint + configFile;
  FILE *f = fopen(full_path.c_str(), "r");
  File file(f, full_path);

  if (!file) {
    config = defaultConfig();
    log(WARNING, "⚙️ Failed to open config file: Using default config");
  } else {
    // Use DynamicJsonDocument to avoid stack overflow - allocates on heap
    JsonDocument loadedConfig;
    DeserializationError error = deserializeJson(loadedConfig, file);
    if (error) {
      config = defaultConfig();
      log(WARNING, "⚙️ Failed to read config file: %s: Using default config",
          error.c_str());
    } else {
      mergeConfig(loadedConfig);
    }
    file.close();
  }

  printConfig();

  readingConfig = false;
}

void ESPWiFi::printConfig() {
  log(INFO, "⚙️  Config File: " + configFile);
  // Serialize directly from config member to avoid stack overflow
  // No need to copy - config is already a member variable (JsonDocument)
  std::string prettyConfig;
  serializeJsonPretty(config, prettyConfig);
  log(DEBUG, "\n" + prettyConfig);
}

void ESPWiFi::saveConfig() {
  initLittleFS();

  if (!littleFsInitialized) {
    log(ERROR, "No filesystem available for saving config");
    return;
  }

  std::string full_path = lfsMountPoint + configFile;
  FILE *f = fopen(full_path.c_str(), "w");
  File file(f, full_path);
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
  if (!webServerStarted) {
    log(ERROR,
        "Cannot start config API /api/config: web server not initialized");
    return;
  }

  HTTPRoute("/api/config", HTTP_GET, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    std::string json;
    serializeJson(espwifi->config, json);
    espwifi->sendJsonResponse(req, 200, json);
    return ESP_OK;
  });

  // Config POST endpoint
  HTTPRoute("/api/config", HTTP_POST, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    if (!espwifi->authorized(req)) {
      espwifi->sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}");
      return ESP_OK;
    }

    // Read request body
    size_t content_len = req->content_len;
    if (content_len > 4096) { // Limit to 4KB
      httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE,
                          "Request body too large");
      return ESP_FAIL;
    }

    char *content = (char *)malloc(content_len + 1);
    if (content == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, content_len);
    if (ret <= 0) {
      free(content);
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        httpd_resp_send_408(req);
      }
      return ESP_FAIL;
    }

    content[content_len] = '\0';
    std::string json_body(content);
    free(content);

    // TODO: Parse and update config
    espwifi->sendJsonResponse(req, 200, "{\"status\":\"ok\"}");
    return ESP_OK;
  });
}

#endif // ESPWiFi_CONFIG
