#ifndef ESPWiFi_CONFIG
#define ESPWiFi_CONFIG

#include "ESPWiFi.h"

void ESPWiFi::readConfig() {

  initLittleFS();
  File file = LittleFS.open(configFile, "r");

  if (!file) {
    log("⚠️  Failed to open config file\nUsing default config");
    config = defaultConfig();
  } else {
    JsonDocument loadedConfig;
    DeserializationError error = deserializeJson(loadedConfig, file);
    if (error) {
      log("⚠️  Failed to read config file: " + String(error.c_str()) +
          "\nUsing default config");
      JsonDocument defaultConfigDoc = defaultConfig();
      mergeConfig(defaultConfigDoc);
    }
    mergeConfig(loadedConfig);
    file.close();
  }

  // OTA - based on partition table
  config["ota"]["enabled"] = isOTAEnabled();

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

void ESPWiFi::mergeConfig(JsonDocument &json) {
  for (JsonPair kv : json.as<JsonObject>()) {
    config[kv.key()] = kv.value();
  }
}

void ESPWiFi::handleConfig() {
#ifdef ESPWiFi_CAMERA_INSTALLED
  cameraConfigHandler();
#else
  config["camera"]["enabled"] = false;
  config["camera"]["installed"] = false;
#endif
}

JsonDocument ESPWiFi::defaultConfig() {
  JsonDocument defaultConfig;

  defaultConfig["mode"] = "accessPoint";
  defaultConfig["mdns"] = "ESPWiFi";

  // Access Point
  String hostname = String(WiFi.getHostname());
  defaultConfig["ap"]["ssid"] = "ESPWiFi-" + hostname;
  defaultConfig["ap"]["password"] = "abcd1234";

  // WiFi Client
  defaultConfig["client"]["ssid"] = "";
  defaultConfig["client"]["password"] = "";

// Camera
#ifdef ESPWiFi_CAMERA_INSTALLED
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

  // RSSI - always enabled
  defaultConfig["rssi"]["displayMode"] = "icon";

  // SD Card - default: disabled
  defaultConfig["sd"]["enabled"] = false;

  // OTA - based on partition table
  defaultConfig["ota"]["enabled"] = isOTAEnabled();

  return defaultConfig;
}

void ESPWiFi::srvConfig() {
  initWebServer();

  // Add OPTIONS handler for /config endpoint
  webServer->on(
      "/config", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on("/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String responseStr;
    serializeJson(config, responseStr);
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", responseStr);
    addCORS(response);
    request->send(response);
  });

  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/config", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (json.isNull()) {
          AsyncWebServerResponse *response = request->beginResponse(
              400, "application/json", "{\"error\":\"EmptyInput\"}");
          addCORS(response);
          request->send(response);
          logError("/config Error parsing JSON: EmptyInput");
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