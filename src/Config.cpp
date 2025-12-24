#ifndef ESPWiFi_CONFIG
#define ESPWiFi_CONFIG

#include "ESPWiFi.h"

void ESPWiFi::readConfig() {
  static bool readingConfig = false;
  if (readingConfig) {
    return; // Prevent recursive calls
  }
  readingConfig = true;

  initLittleFS();
  File file = LittleFS.open(configFile, "r");

  if (!file) {
    log(WARNING, "⚠️  Failed to open config file\nUsing default config");
    config = defaultConfig();
  } else {
    JsonDocument loadedConfig;
    DeserializationError error = deserializeJson(loadedConfig, file);
    if (error) {
      log(WARNING, "⚠️  Failed to read config file: %s\nUsing default config",
          error.c_str());
      config = defaultConfig();
    } else {
      mergeConfig(loadedConfig);
    }
    file.close();
  }

  // OTA - based on partition table
  config["ota"]["enabled"] = isOTAEnabled();

  // Bluetooth
#if defined(CONFIG_BT_ENABLED)
  config["bluetooth"]["installed"] = true;
#else
  config["bluetooth"]["installed"] = false;
  config["bluetooth"]["enabled"] = false;
#endif

  config["hostname"] = String(WiFi.getHostname());

  log(INFO, "⚙️  Config Loaded:");
  log(DEBUG, "\tFile: %s", configFile.c_str());

  printConfig();
  file.close();

  readingConfig = false;
}

void ESPWiFi::saveConfig() {
  initLittleFS();
  File file = LittleFS.open(configFile, "w");
  if (!file) {
    log(ERROR, " Failed to open config file for writing");
    return;
  }
  size_t written = serializeJson(config, file);
  file.close();
  if (written == 0) {
    log(ERROR, " Failed to write config JSON to file");
    return;
  }
  if (config["log"]["enabled"].as<bool>()) {
    log(INFO, "💾 Config Saved: %s", configFile.c_str());
    printConfig();
  }
}

void ESPWiFi::printConfig() {
  String prettyConfig;
  serializeJsonPretty(config, prettyConfig);
  log(DEBUG, "\n" + prettyConfig);
}

void ESPWiFi::mergeConfig(JsonDocument &json) {
  for (JsonPair kv : json.as<JsonObject>()) {
    config[kv.key()] = kv.value();
  }
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

  defaultConfig["deviceName"] = "ESPWiFi";

  // WiFi configuration - nested under wifi
  defaultConfig["wifi"]["mode"] = "accessPoint";
  defaultConfig["wifi"]["enabled"] = true;

  // Access Point
  String ssid = "ESPWiFi-" + String(WiFi.getHostname());
  defaultConfig["wifi"]["ap"]["ssid"] = ssid;
  defaultConfig["wifi"]["ap"]["password"] = "espwifi123";

  // WiFi Client
  defaultConfig["wifi"]["client"]["ssid"] = "";
  defaultConfig["wifi"]["client"]["password"] = "";

// Camera
#ifdef ESPWiFi_CAMERA
  defaultConfig["camera"]["installed"] = true;
#else
  defaultConfig["camera"]["installed"] = false;
#endif
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

  // SD Card - default: disabled
  defaultConfig["sd"]["enabled"] = false;

  // OTA - based on partition table
  defaultConfig["ota"]["enabled"] = isOTAEnabled();

  // Auth
  defaultConfig["auth"]["token"] = "";
  defaultConfig["auth"]["enabled"] = false;
  defaultConfig["auth"]["password"] = "admin";
  defaultConfig["auth"]["username"] = "admin";

  // Bluetooth (BLE - works on ESP32-S3 and ESP32-C3)
  defaultConfig["bluetooth"]["enabled"] = false;
#if defined(CONFIG_BT_ENABLED)
  defaultConfig["bluetooth"]["installed"] = true;
#else
  defaultConfig["bluetooth"]["installed"] = false;
#endif

  // Logging
  defaultConfig["log"]["enabled"] = true;
  defaultConfig["log"]["level"] = "info"; // debug, info, warning, error

  return defaultConfig;
}

void ESPWiFi::srvConfig() {
  initWebServer();

  // Add OPTIONS handler for /config endpoint
  webServer->on(
      "/config", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on("/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
      return;
    }
    String responseStr;
    serializeJson(config, responseStr);
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", responseStr);
    addCORS(response);
    request->send(response);
  });

  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/config", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
        if (json.isNull()) {
          AsyncWebServerResponse *response = request->beginResponse(
              400, "application/json", "{\"error\":\"EmptyInput\"}");
          addCORS(response);
          request->send(response);
          log(ERROR, "/config Error parsing JSON: EmptyInput");
          return;
        }

        JsonDocument jsonDoc = json;
        mergeConfig(jsonDoc);

        if (request->method() == HTTP_PUT) {
          saveConfig();
        }

        handleConfig();

        String responseStr;
        serializeJson(config, responseStr);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", responseStr);
        addCORS(response);
        request->send(response);
      }));
}
#endif // ESPWiFi_CONFIG