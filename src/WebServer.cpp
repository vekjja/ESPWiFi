#include <AsyncJson.h>

#include "ESPWiFi.h"

// Helper: Add CORS headers to a response
void ESPWiFi::addCORS(AsyncWebServerResponse *response) {
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
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
  initWebServer();
  // Serve the root file
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

  // Device info endpoint
  webServer->on("/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String info =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESPWiFi "
        "Info</title>";
    info +=
        "<style>body{font-family:sans-serif;background:#181a1b;color:#e8eaed;"
        "margin:0;padding:2em;}h2{color:#7FF9E9;}h3{margin-top:2em;color:#"
        "bdbdbd;}table{background:#23272a;border-radius:8px;box-shadow:0 2px "
        "8px "
        "#0008;border-collapse:collapse;width:100%;max-width:600px;margin:auto;"
        "}th,td{padding:10px "
        "16px;text-align:left;}th{background:#222c36;color:#7FF9E9;}tr:nth-"
        "child(even){background:#202124;}tr:hover{background:#263238;}pre{"
        "background:#23272a;color:#e8eaed;padding:1em;border-radius:8px;"
        "overflow-x:auto;max-width:600px;margin:auto;border:1px solid "
        "#333;}a{color:#7FF9E9;}::-webkit-scrollbar{background:#23272a;}::-"
        "webkit-scrollbar-thumb{background:#333;border-radius:8px;}</style></"
        "head><body>";
    info += "<h2>📡 ESPWiFi Device Info</h2>";
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    seconds = seconds % 60;
    minutes = minutes % 60;
    hours = hours % 24;
    String uptime = String(days) + "d " + String(hours) + "h " +
                    String(minutes) + "m " + String(seconds) + "s";
    info +=
        "<h3>🌐 Network</h3><table><tr><th>Property</th><th>Value</th></tr>";
    info += "<tr><td>IP Address</td><td>" + WiFi.localIP().toString() +
            "</td></tr>";
    info += "<tr><td>MAC Address</td><td>" + WiFi.macAddress() + "</td></tr>";
#if defined(ESP8266) || defined(ESP32)
    if (WiFi.isConnected()) {
      info += "<tr><td>SSID</td><td>" + WiFi.SSID() + "</td></tr>";
      info += "<tr><td>RSSI</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
    }
    info +=
        "<tr><td>Hostname</td><td>" + String(WiFi.getHostname()) + "</td></tr>";
#endif
    info += "<tr><td>mDNS Name</td><td>" + config["mdns"].as<String>() +
            ".local</td></tr>";
    info += "<tr><td>Uptime</td><td>" + uptime + "</td></tr></table>";
    info += "<h3>🔩 Chip</h3><table><tr><th>Property</th><th>Value</th></tr>";
#if defined(ESP8266)
    info += "<tr><td>Chip ID</td><td>" + String(ESP.getChipId()) + "</td></tr>";
    info += "<tr><td>Boot Version</td><td>" + String(ESP.getBootVersion()) +
            "</td></tr>";
    info +=
        "<tr><td>Boot Mode</td><td>" + String(ESP.getBootMode()) + "</td></tr>";
    info += "<tr><td>Flash Chip ID</td><td>" +
            String(ESP.getFlashChipId(), HEX) + "</td></tr>";
#elif defined(ESP32)
    info += "<tr><td>Model</td><td>" + String(ESP.getChipModel()) + "</td></tr>";
    info += "<tr><td>Revision</td><td>" + String(ESP.getChipRevision()) + "</td></tr>";
#endif
    info += "<tr><td>CPU Frequency</td><td>" + String(ESP.getCpuFreqMHz()) +
            " MHz</td></tr>";
#if defined(ESP8266)
    info += "<tr><td>SDK Version</td><td>" + String(ESP.getSdkVersion()) +
            "</td></tr>";
#elif defined(ESP32)
    info += "<tr><td>SDK Version</td><td>" + String(ESP.getSdkVersion()) + "</td></tr>";
#endif
    info +=
        "</table><h3>💾 Memory</h3><table><tr><th>Type</th><th>Value</th></tr>";
    info += "<tr><td>Free Heap</td><td>" +
            String(ESP.getFreeHeap() / 1048576.0, 2) + " MB</td></tr>";
#if defined(ESP32)
    info += "<tr><td>Free PSRAM</td><td>" +
            String(ESP.getFreePsram() / 1048576.0, 2) + " MB</td></tr>";
#endif
    info +=
        "</table><h3>🗄️ "
        "Storage</h3><table><tr><th>Type</th><th>Value</th></tr>";
    info += "<tr><td>Sketch Size</td><td>" +
            String(ESP.getSketchSize() / 1048576.0, 2) + " MB</td></tr>";
    info += "<tr><td>Free Sketch Space</td><td>" +
            String(ESP.getFreeSketchSpace() / 1048576.0, 2) + " MB</td></tr>";
    info += "<tr><td>Flash Chip Size</td><td>" +
            String(ESP.getFlashChipSize() / 1048576.0, 2) + " MB</td></tr>";
    info += "<tr><td>Flash Chip Speed</td><td>" +
            String(ESP.getFlashChipSpeed() / 1000000) + " MHz</td></tr>";
    info += "<tr><td>Flash Chip Mode</td><td>" +
            String(ESP.getFlashChipMode() == FM_QIO
                       ? "QIO"
                       : (ESP.getFlashChipMode() == FM_QOUT
                              ? "QOUT"
                              : (ESP.getFlashChipMode() == FM_DIO ? "DIO"
                                                                  : "DOUT"))) +
            "</td></tr>";
#if defined(ESP8266)
    FSInfo fs_info;
    LittleFS.info(fs_info);
    info += "<tr><td>FS Total</td><td>" +
            String(fs_info.totalBytes / 1048576.0, 2) + " MB</td></tr>";
    info += "<tr><td>FS Used</td><td>" +
            String(fs_info.usedBytes / 1048576.0, 2) + " MB</td></tr>";
#elif defined(ESP32)
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    info += "<tr><td>FS Total</td><td>" + String(totalBytes / 1048576.0, 2) + " MB</td></tr>";
    info += "<tr><td>FS Used</td><td>" + String(usedBytes / 1048576.0, 2) + " MB</td></tr>";
#endif
    info +=
        "</table><h3>📝 "
        "Sketch</h3><table><tr><th>Property</th><th>Value</th></tr>";
    info += "<tr><td>MD5</td><td>" + ESP.getSketchMD5() + "</td></tr></table>";
    info += "<h3>⚙️ Config</h3>";
    String configStr;
    serializeJsonPretty(config, configStr);
    info += "<pre>" + configStr + "</pre></body></html>";
    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", info);
    addCORS(response);
    request->send(response);
  });

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

  // /config endpoint
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
          Serial.println("❌ /config HTTP_POST Error parsing JSON: EmptyInput");
          return;
        }
        // Merge posted JSON into config
        for (JsonPair kv : json.as<JsonObject>()) {
          config[kv.key()] = kv.value();
        }
        saveConfig();
        String responseStr;
        serializeJson(config, responseStr);
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", responseStr);
        addCORS(response);
        request->send(response);
      }));

  // /restart endpoint
  webServer->on(
      "/restart", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });
  webServer->on("/restart", HTTP_GET, [this](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", "{\"success\":true}");
    addCORS(response);
    request->send(response);
    Serial.println("🔄 Restarting...");
    delay(1000);
    ESP.restart();
  });

  // /files endpoint (robust, dark mode, correct subdir links, parent dir nav)
  webServer->on("/files", HTTP_GET, [this](AsyncWebServerRequest *request) {
    String html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESPWiFi "
        "Files</title>";
    html +=
        "<style>body{font-family:sans-serif;background:#181a1b;color:#e8eaed;"
        "margin:0;padding:2em;}h2{color:#7FF9E9;}ul{background:#23272a;border-"
        "radius:8px;box-shadow:0 2px 8px "
        "#0008;max-width:700px;margin:auto;padding:1em;}li{margin:0.5em "
        "0;}a{color:#7FF9E9;text-decoration:none;font-weight:bold;}a:hover{"
        "text-decoration:underline;} "
        ".folder{color:#7FF9E9;}::-webkit-scrollbar{background:#23272a;}::-"
        "webkit-scrollbar-thumb{background:#333;border-radius:8px;} "
        ".folder{font-weight:bold;} .file{color:#e8eaed;}</style></head><body>";
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

  webServer->begin();
  Serial.println("🕸️  HTTP Web Server Running");
  Serial.print("\tURL: http://" + WiFi.localIP().toString() + "\n");
  Serial.print("\tURL: http://" + config["mdns"].as<String>() + ".local\n");
}