#ifndef ESPWiFi_WEB_SERVER_H
#define ESPWiFi_WEB_SERVER_H
#include <AsyncJson.h>

#include "ESPWiFi.h"

// Helper: Add CORS headers to a response
void ESPWiFi::addCORS(AsyncWebServerResponse *response) {
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods",
                      "GET, POST, OPTIONS, PUT, DELETE");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type");
}

void ESPWiFi::handleCorsPreflight(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(204);
  addCORS(response);
  request->send(response);
}

void ESPWiFi::initWebServer() {
  if (!webServer) {
    webServer = new AsyncWebServer(80);
  }
}

void ESPWiFi::startWebServer() {
  if (webServerStarted) {
    return;
  }
  initWebServer();
  srvAll();
  webServer->begin();
  webServerStarted = true;
  log("🗄️  HTTP Web Server Started:");
  logf("\tURL: http://%s\n", WiFi.localIP().toString().c_str());
  logf("\tURL: http://%s.local\n", config["mdns"].as<String>().c_str());
}

void ESPWiFi::srvRoot() {
  initWebServer();
  // Serve index.html at root
  webServer->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/index.html")) {
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, "/index.html", "text/html");
      addCORS(response);
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/plain", "File Not Found");
      addCORS(response);
      request->send(response);
    }
  });
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

        JsonObject jsonObject = json.as<JsonObject>();
        mergeConfig(jsonObject);

        if (request->method() == HTTP_PUT) {
          saveConfig();
        }

        if (configUpdateCallback) {
          configUpdateCallback();
        }

        cameraConfigHandler();
        rssiConfigHandler();

        String responseStr;
        serializeJson(config, responseStr);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", responseStr);
        addCORS(response);
        request->send(response);
      }));
}

void ESPWiFi::srvRestart() {
  initWebServer();
  webServer->on("/restart", HTTP_GET, [this](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/plain", "Restarting...");
    addCORS(response);
    request->send(response);
    delay(1000);
    ESP.restart();
  });
}

void ESPWiFi::srvLog() {
  initWebServer();
  webServer->on("/log", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (fs && fs->exists(logFilePath)) {
      AsyncWebServerResponse *response = request->beginResponse(
          *fs, logFilePath,
          "text/plain; charset=utf-8"); // Set UTF-8 encoding
      addCORS(response);
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/plain; charset=utf-8",
                                 "Log file not found"); // Set UTF-8 encoding
      addCORS(response);
      request->send(response);
    }
  });
}

void ESPWiFi::srvInfo() {
  initWebServer();
  // Device info endpoint
  webServer->on("/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
    JsonDocument jsonDoc;
    jsonDoc["uptime"] = millis() / 1000;
    jsonDoc["ip"] = WiFi.localIP().toString();
    jsonDoc["mac"] = WiFi.macAddress();
    jsonDoc["ap_ssid"] = WiFi.softAPSSID();
    if (WiFi.isConnected()) {
      jsonDoc["client_ssid"] = WiFi.SSID();
      jsonDoc["rssi"] = WiFi.RSSI();
    }
    jsonDoc["hostname"] = WiFi.getHostname();
    jsonDoc["mdns"] = config["mdns"].as<String>() + ".local";
#ifdef ESP32
    jsonDoc["chip"] = String(ESP.getChipModel());
#else
    jsonDoc["chip"] = String(ESP.getChipId());
#endif
    jsonDoc["sdk_version"] = String(ESP.getSdkVersion());
    jsonDoc["free_heap"] = ESP.getFreeHeap();

#ifdef ESP8266
    FSInfo fs_info;
    LittleFS.info(fs_info);
    size_t totalBytes = fs_info.totalBytes;
    size_t usedBytes = fs_info.usedBytes;
#elif defined(ESP32)
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
#endif
    jsonDoc["littlefs_free"] = totalBytes - usedBytes;
    jsonDoc["littlefs_used"] = usedBytes;
    jsonDoc["littlefs_total"] = totalBytes;
    jsonDoc["config"] = config.as<JsonObject>();

    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", jsonResponse);
    addCORS(response);
    request->send(response);
  });
}

void ESPWiFi::srvFiles() {
  initWebServer();

  // Generic file requests - updated to handle both LittleFS and SD card
  webServer->onNotFound([this](AsyncWebServerRequest *request) {
    // Handle CORS preflight requests
    if (request->method() == HTTP_OPTIONS) {
      handleCorsPreflight(request);
      return;
    }

    String path = request->url();

    // Check for filesystem prefix (e.g., /sd/path/to/file or
    // /littlefs/path/to/file)
    String fsPrefix = "";
    String actualPath = path;

    if (path.startsWith("/sd/")) {
      fsPrefix = "/sd";
      actualPath = path.substring(3); // Remove "/sd"
      if (!actualPath.startsWith("/"))
        actualPath = "/" + actualPath;
    } else if (path.startsWith("/littlefs/")) {
      fsPrefix = "/littlefs";
      actualPath = path.substring(10); // Remove "/littlefs"
      if (!actualPath.startsWith("/"))
        actualPath = "/" + actualPath;
    }

    // Try to serve from the appropriate filesystem
    if (fsPrefix == "/sd" && sdCardInitialized && fs &&
        fs->exists(actualPath)) {
      String contentType = getContentType(actualPath);
      AsyncWebServerResponse *response =
          request->beginResponse(*fs, actualPath, contentType);
      addCORS(response);
      request->send(response);
    } else if (fsPrefix == "/littlefs" && LittleFS.exists(actualPath)) {
      String contentType = getContentType(actualPath);
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, actualPath, contentType);
      addCORS(response);
      request->send(response);
    } else if (fsPrefix == "" && LittleFS.exists(path)) {
      // Default to LittleFS for paths without prefix
      String contentType = getContentType(path);
      AsyncWebServerResponse *response =
          request->beginResponse(LittleFS, path, contentType);
      addCORS(response);
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/plain", "404: Not Found");
      addCORS(response);
      request->send(response);
    }
  });

  webServer->on("/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String html = "<!DOCTYPE html><html><head><meta "
                  "charset='UTF-8'><title>ESPWiFi Files</title>";
    html += "<style>body{font-family:sans-serif;background:#181a1b;color:#"
            "e8eaed;margin:0;padding:2em;}";
    html += "h2{color:#7FF9E9;}h3{color:#7FF9E9;margin-top:2em;border-bottom:"
            "2px solid #7FF9E9;padding-bottom:0.5em;}";
    html += "ul{background:#23272a;border-radius:8px;box-shadow:0 2px 8px "
            "#0008;max-width:700px;margin:auto;padding:1em;}";
    html += "li{margin:0.5em "
            "0;}a{color:#7FF9E9;text-decoration:none;font-weight:bold;}a:hover{"
            "text-decoration:underline;}";
    html += ".folder{color:#7FF9E9;font-weight:bold;}.file{color:#e8eaed;}";
    html += ".fs-section{margin-bottom:2em;}.fs-header{background:#2d3748;"
            "padding:1em;border-radius:8px;margin-bottom:1em;}";
    html += "::-webkit-scrollbar{background:#23272a;}::-webkit-scrollbar-thumb{"
            "background:#333;border-radius:8px;}";
    html += "</style></head><body>";

    html += "<h2>📁 ESPWiFi Files</h2>";

    // Get filesystem and path parameters
    String fsParam = "";
    String path = "/";

    if (request->hasParam("fs")) {
      fsParam = request->getParam("fs")->value();
    }
    if (request->hasParam("dir")) {
      path = request->getParam("dir")->value();
      if (!path.startsWith("/"))
        path = "/" + path;
    }

    // Helper function to generate file listing HTML
    auto generateFileListing = [&](FS *filesystem, const String &fsName,
                                   const String &fsPrefix,
                                   const String &currentPath) -> String {
      String result = "";

      if (!filesystem) {
        result += "<div class='fs-section'>";
        result += "<div class='fs-header'><h3>💾 " + fsName +
                  " (Not Available)</h3></div>";
        result += "<p>File system not mounted or available.</p></div>";
        return result;
      }

      File root = filesystem->open(currentPath, "r");
      if (!root || !root.isDirectory()) {
        result += "<div class='fs-section'>";
        result += "<div class='fs-header'><h3>💾 " + fsName + "</h3></div>";
        result += "<p>Directory not found: " + currentPath + "</p></div>";
        return result;
      }

      result += "<div class='fs-section'>";
      result += "<div class='fs-header'><h3>💾 " + fsName + "</h3></div>";
      result += "<ul>";

      // Add parent directory link if not root
      if (currentPath != "/") {
        String parent = currentPath;
        if (parent.endsWith("/"))
          parent = parent.substring(0, parent.length() - 1);
        int lastSlash = parent.lastIndexOf('/');
        if (lastSlash > 0) {
          parent = parent.substring(0, lastSlash);
        } else {
          parent = "/";
        }
        String parentQuery = "?fs=" + fsPrefix + "&dir=" + parent;
        if (parent.startsWith("/"))
          parent = parent.substring(1);
        result += "<li class='folder'>⬆️ <a href='/files" + parentQuery +
                  "'>../</a></li>";
      }

      File file = root.openNextFile();
      while (file) {
        String fname = String(file.name());
        String displayName = fname;
        if (fname.startsWith(currentPath) && currentPath != "/") {
          displayName = fname.substring(currentPath.length());
          if (displayName.startsWith("/"))
            displayName = displayName.substring(1);
        }
        if (displayName == "")
          displayName = fname;

        if (file.isDirectory()) {
          String subdirPath = currentPath;
          if (!subdirPath.endsWith("/"))
            subdirPath += "/";
          subdirPath += displayName;
          String subdirQuery = "?fs=" + fsPrefix + "&dir=" + subdirPath;
          if (subdirPath.startsWith("/"))
            subdirQuery = "?fs=" + fsPrefix + "&dir=" + subdirPath.substring(1);
          result += "<li class='folder'>📁 <a href='/files" + subdirQuery +
                    "'>" + displayName + "/</a></li>";
        } else {
          String filePath = fsPrefix + currentPath;
          if (!filePath.endsWith("/"))
            filePath += "/";
          filePath += displayName;
          if (!filePath.startsWith("/"))
            filePath = "/" + filePath;
          result += "<li class='file'>📄 <a href='" + filePath +
                    "' target='_blank'>" + displayName + "</a></li>";
        }
        file = root.openNextFile();
      }

      result += "</ul></div>";
      return result;
    };

    // Show LittleFS files
    if (fsParam == "" || fsParam == "littlefs") {
      html += generateFileListing(&LittleFS, "LittleFS", "littlefs", path);
    }

    // Show SD Card files (if available)
    if (fsParam == "" || fsParam == "sd") {
      FS *sdFS = nullptr;
      if (sdCardInitialized && fs) {
        sdFS = fs;
      }
      html += generateFileListing(sdFS, "SD Card", "sd", path);
    }

    html += "</body></html>";

    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", html);
    addCORS(response);
    request->send(response);
  });
}

void ESPWiFi::srvOTA() {
  initWebServer();

  // OTA start endpoint (initialize update)
  webServer->on("/ota/start", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleOTAStart(request);
  });

  // OTA reset endpoint (reset stuck updates)
  webServer->on("/ota/reset", HTTP_GET, [this](AsyncWebServerRequest *request) {
    resetOTAState();
    request->send(200, "text/plain", "OTA state reset");
  });

  // OTA progress endpoint
  webServer->on(
      "/ota/progress", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument jsonDoc;
        jsonDoc["in_progress"] = otaInProgress;
        jsonDoc["current_size"] = otaCurrentSize;
        jsonDoc["total_size"] = otaTotalSize;
        jsonDoc["progress"] = 0; // Will be calculated below

        // Calculate progress percentage if we have total size and OTA is in
        // progress
        if (otaInProgress && otaTotalSize > 0) {
          int progress = (otaCurrentSize * 100) / otaTotalSize;
          jsonDoc["progress"] = progress;
        }

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", jsonResponse);
        addCORS(response);
        request->send(response);
      });

  // OTA upload endpoint (actual file upload)
  webServer->on(
      "/ota/upload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleOTAUpdate(request, filename, index, data, len, final);
      });

  // Filesystem update endpoint
  webServer->on(
      "/ota/fsupload", HTTP_POST,
      [this](AsyncWebServerRequest *request) {
        // This will be handled by the upload handler
      },
      [this](AsyncWebServerRequest *request, String filename, size_t index,
             uint8_t *data, size_t len, bool final) {
        handleFSUpdate(request, filename, index, data, len, final);
      });

  // OTA status endpoint
  webServer->on(
      "/ota/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument jsonDoc;
        jsonDoc["firmware_size"] = ESP.getSketchSize();
        jsonDoc["free_space"] = ESP.getFreeSketchSpace();
        jsonDoc["sdk_version"] = String(ESP.getSdkVersion());
        jsonDoc["chip_model"] = String(ESP.getChipModel());
        jsonDoc["ota_start_url"] =
            "http://" + WiFi.localIP().toString() + "/ota/start";
        jsonDoc["ota_upload_url"] =
            "http://" + WiFi.localIP().toString() + "/ota/upload";
        jsonDoc["fs_update_url"] =
            "http://" + WiFi.localIP().toString() + "/ota/fsupload";

        String jsonResponse;
        serializeJson(jsonDoc, jsonResponse);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", jsonResponse);
        addCORS(response);
        request->send(response);
      });

  // OTA update page endpoint
  webServer->on("/ota", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleOTAHtml(request);
  });
}

void ESPWiFi::srvGPIO() {
  initWebServer();
  // CORS preflight
  webServer->on("/gpio", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(204);
    addCORS(response);
    request->send(response);
  });

  // JSON POST handler
  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/gpio", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject reqJson = json.as<JsonObject>();
        AsyncWebServerResponse *response = nullptr;
        int pinNum = reqJson["num"] | -1;
        String mode = reqJson["mode"] | "";
        String state = reqJson["state"] | "";
        int duty = reqJson["duty"] | 0;
        bool isDelete = reqJson["delete"] | false;
        mode.toLowerCase();
        state.toLowerCase();

        String errorMsg;
        if (pinNum < 0) {
          errorMsg = "{\"error\":\"Missing pin number\"}";
          response = request->beginResponse(400, "application/json", errorMsg);
          addCORS(response);
          request->send(response);
          return;
        }

        if (mode == "out" || mode == "output" || mode == "pwm") {
          pinMode(pinNum, OUTPUT);
        } else if (mode == "input" || mode == "in") {
          pinMode(pinNum, INPUT);
        } else {
          errorMsg = String("{\"error\":\"Invalid mode: ") + mode + "\"}";
          response = request->beginResponse(400, "application/json", errorMsg);
          addCORS(response);
          request->send(response);
          return;
        }
        if (state == "high") {
          if (mode == "pwm") {
            analogWrite(pinNum, duty);
          } else {
            digitalWrite(pinNum, HIGH);
          }
        } else if (state == "low") {
          digitalWrite(pinNum, LOW);
        } else {
          errorMsg = String("{\"error\":\"Invalid state: ") + state + "\"}";
          response = request->beginResponse(400, "application/json", errorMsg);
          addCORS(response);
          request->send(response);
          return;
        }
        String okMsg = "{\"status\":\"Success\"}";
        response = request->beginResponse(200, "application/json", okMsg);
        addCORS(response);
        request->send(response);

        // Log the operation - only include duty for PWM mode
        String logMsg = "📍 GPIO " + String(pinNum) + " " + mode + " " + state;
        if (mode == "pwm") {
          logMsg += " " + String(duty);
        }
        log(logMsg);
      }));
  // log("📍 GPIO Started:");
  // log("\tPOST /gpio");
}

void ESPWiFi::srvAll() {
  srvLog();
  srvRoot();
  srvOTA();
  srvInfo();
  srvGPIO();
  srvFiles();
  srvConfig();
  srvRestart();
}

#endif // ESPWiFi_WEB_SERVER_H