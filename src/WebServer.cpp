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
  static bool webServerStarted = false;
  if (webServerStarted) {
    return;
  }
  initWebServer();
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
  // /log endpoint
  webServer->on("/log", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (LittleFS.exists(logFile)) {
      AsyncWebServerResponse *response = request->beginResponse(
          LittleFS, logFile,
          "text/plain; charset=utf-8");  // Set UTF-8 encoding
      addCORS(response);
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/plain; charset=utf-8",
                                 "Log file not found");  // Set UTF-8 encoding
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

  // Generic file requests
  webServer->onNotFound([this](AsyncWebServerRequest *request) {
    String path = request->url();
    if (LittleFS.exists(path)) {
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

  // /files endpoint (robust, dark mode, correct subdir links, parent dir nav)
  webServer->on("/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESPWiFi "
        "Files</title>";
    html +=
        "<style>body{font-family:sans-serif;background:#181a1b;color:#e8eaed;"
        "margin:0;padding:2em;}h2{color:#7FF9E9;}ul{background:#23272a;"
        "border-"
        "radius:8px;box-shadow:0 2px 8px "
        "#0008;max-width:700px;margin:auto;padding:1em;}li{margin:0.5em "
        "0;}a{color:#7FF9E9;text-decoration:none;font-weight:bold;}a:hover{"
        "text-decoration:underline;} "
        ".folder{color:#7FF9E9;}::-webkit-scrollbar{background:#23272a;}::-"
        "webkit-scrollbar-thumb{background:#333;border-radius:8px;} "
        ".folder{font-weight:bold;} "
        ".file{color:#e8eaed;}</style></head><body>";
    html += "<h2>📁 ESPWiFi Files</h2>";
    String path = "/";
    if (request->hasParam("dir")) {
      path = request->getParam("dir")->value();
      if (!path.startsWith("/")) path = "/" + path;
    }
    File root = LittleFS.open(path, "r");
    if (!root || !root.isDirectory()) {
      html += "<p>Directory not found: " + path + "</p></body></html>";
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/html", html);
      addCORS(response);
      request->send(response);
      return;
    }
    html += "<ul>";
    // Add parent directory link if not root
    if (path != "/") {
      String parent = path;
      if (parent.endsWith("/"))
        parent = parent.substring(0, parent.length() - 1);
      int lastSlash = parent.lastIndexOf('/');
      if (lastSlash > 0) {
        parent = parent.substring(0, lastSlash);
      } else {
        parent = "/";
      }
      html += "<li class='folder'>⬆️ <a href='/files?dir=" + parent +
              "'>../</a></li>";
    }
    File file = root.openNextFile();
    while (file) {
      String fname = String(file.name());
      String displayName = fname;
      if (fname.startsWith(path) && path != "/") {
        displayName = fname.substring(path.length());
        if (displayName.startsWith("/")) displayName = displayName.substring(1);
      }
      if (displayName == "") displayName = fname;
      if (file.isDirectory()) {
        // Build subdirectory path for query string
        String subdirPath = path;
        if (!subdirPath.endsWith("/")) subdirPath += "/";
        subdirPath += displayName;
        // Remove leading slash for query string
        if (subdirPath.startsWith("/")) subdirPath = subdirPath.substring(1);
        html += "<li class='folder'>📁 <a href='/files?dir=" + subdirPath +
                "'>" + displayName + "/</a></li>";
      } else {
        // Build file path for link
        String filePath = path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += displayName;
        // Ensure single leading slash
        if (!filePath.startsWith("/")) filePath = "/" + filePath;
        html += "<li class='file'>📄 <a href='" + filePath +
                "' target='_blank'>" + displayName + "</a></li>";
      }
      file = root.openNextFile();
    }
    html += "</ul></body></html>";
    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", html);
    addCORS(response);
    request->send(response);
  });
}

void ESPWiFi::srvAll() {
  srvLog();
  srvRoot();
  srvInfo();
  srvFiles();
  srvConfig();
  srvRestart();
}