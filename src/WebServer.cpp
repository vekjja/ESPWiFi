#ifndef ESPWIFI_WEBSERVER_H
#define ESPWIFI_WEBSERVER_H

#include "ESPWiFi.h"

// Generic CORS handler
void ESPWiFi::handleCorsPreflight() {
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  webServer.send(204);  // No content
};

void ESPWiFi::startWebServer() {
  // Serve the root file
  webServer.on("/", HTTP_GET, [this]() {
    File file = LittleFS.open("/index.html", "r");
    if (file) {
      webServer.sendHeader("Access-Control-Allow-Origin", "*");
      webServer.streamFile(file, "text/html");
      file.close();
    } else {
      webServer.send(404, "text/plain", "File Not Found");
    }
  });

  webServer.on("/info", HTTP_GET, [this]() {
    String info =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESPWiFi "
        "Info</title>";
    info +=
        "<style>body{font-family:sans-serif;background:#f4f4f4;margin:0;"
        "padding:2em;}h2{color:#1976d2;}h3{margin-top:2em;color:#333;}table{"
        "background:#fff;border-radius:8px;box-shadow:0 2px 8px "
        "#0001;border-collapse:collapse;width:100%;max-width:600px;margin:auto;"
        "}th,td{padding:10px "
        "16px;text-align:left;}th{background:#f0f0f0;}tr:nth-child(even){"
        "background:#fafafa;}tr:hover{background:#e0f7fa;}pre{background:#222;"
        "color:#fff;padding:1em;border-radius:8px;overflow-x:auto;max-width:"
        "600px;margin:auto;}</style></head><body>";
    info += "<h2>📡 ESPWiFi Device Info</h2>";
    // Uptime
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    seconds = seconds % 60;
    minutes = minutes % 60;
    hours = hours % 24;
    String uptime = String(days) + "d " + String(hours) + "h " +
                    String(minutes) + "m " + String(seconds) + "s";
    // Network Info
    info += "<h3>🌐 Network</h3><table>";
    info += "<tr><th>Property</th><th>Value</th></tr>";
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
    info += "<tr><td>Uptime</td><td>" + uptime + "</td></tr>";
    info += "</table>";
    // Chip Info
    info += "<h3>🔩 Chip</h3><table>";
    info += "<tr><th>Property</th><th>Value</th></tr>";
    info +=
        "<tr><td>Model</td><td>" + String(ESP.getChipModel()) + "</td></tr>";
    info += "<tr><td>Revision</td><td>" + String(ESP.getChipRevision()) +
            "</td></tr>";
    info += "<tr><td>CPU Frequency</td><td>" + String(ESP.getCpuFreqMHz()) +
            " MHz</td></tr>";
#if defined(ESP8266)
    info += "<tr><td>SDK Version</td><td>" + String(ESP.getSdkVersion()) +
            "</td></tr>";
#elif defined(ESP32)
    info += "<tr><td>SDK Version</td><td>" + String(ESP.getSdkVersion()) + "</td></tr>";
#endif
    info += "</table>";
    // Memory Info
    info += "<h3>💾 Memory</h3><table>";
    info += "<tr><th>Type</th><th>Value</th></tr>";
    info += "<tr><td>Free Heap</td><td>" +
            String(ESP.getFreeHeap() / 1048576.0, 2) + " MB</td></tr>";
#if defined(ESP32)
    info += "<tr><td>Free PSRAM</td><td>" +
            String(ESP.getFreePsram() / 1048576.0, 2) + " MB</td></tr>";
#endif
    info += "</table>";
    // Storage Info
    info += "<h3>🗄️ Storage</h3><table>";
    info += "<tr><th>Type</th><th>Value</th></tr>";
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
    info += "</table>";
    // Sketch Info
    info += "<h3>📝 Sketch</h3><table>";
    info += "<tr><th>Property</th><th>Value</th></tr>";
    info += "<tr><td>MD5</td><td>" + ESP.getSketchMD5() + "</td></tr>";
    info += "</table>";
    // Config Info
    info += "<h3>⚙️ Config</h3>";
    String configStr;
    serializeJsonPretty(config, configStr);
    info += "<pre>" + configStr + "</pre>";
    info += "</body></html>";
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "text/html", info);
  });

  // Handle generic file requests
  webServer.onNotFound([this]() {
    // Handle preflight (OPTIONS) requests
    if (webServer.method() == HTTP_OPTIONS) {
      handleCorsPreflight();
      return;
    }

    // Serve the requested file
    String path = webServer.uri();
    if (LittleFS.exists(path)) {
      File file = LittleFS.open(path, "r");
      String contentType = getContentType(path);  // Determine the MIME type
      webServer.sendHeader("Access-Control-Allow-Origin", "*");
      webServer.streamFile(file, contentType);
      file.close();
    } else {
      webServer.sendHeader("Access-Control-Allow-Origin", "*");
      webServer.send(404, "text/plain", "404: Not Found");
    }
  });

  // Handle /config endpoint
  webServer.on("/config", HTTP_OPTIONS, [this]() { handleCorsPreflight(); });
  webServer.on("/config", HTTP_GET, [this]() {
    String response;
    serializeJson(config, response);
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "application/json", response);
  });
  webServer.on("/config", HTTP_POST, [this]() {
    String body = webServer.arg("plain");
    DeserializationError error = deserializeJson(config, body);
    if (error) {
      webServer.sendHeader("Access-Control-Allow-Origin", "*");
      webServer.send(400, "application/json",
                     "{\"error\":\"" + String(error.c_str()) + "\"}");
    } else {
      saveConfig();
      String response;
      serializeJson(config, response);
      webServer.sendHeader("Access-Control-Allow-Origin", "*");
      webServer.send(200, "application/json", response);
    }
  });

  // Handle /restart endpoint
  webServer.on("/restart", HTTP_OPTIONS, [this]() { handleCorsPreflight(); });
  webServer.on("/restart", HTTP_GET, [this]() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "application/json", "{\"success\":true}");
    Serial.println("🔄 Restarting...");
    delay(1000);
    ESP.restart();
  });

  // Handle file listing
  webServer.on("/files", HTTP_GET, [this]() { listFilesHandler(); });

  // Start the server
  webServer.begin();
};

void ESPWiFi::listFilesHandler() {
  String path = "/";
  File root = LittleFS.open(path, "r");
  String html =
      "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESPWiFi "
      "Files</title>";
  html +=
      "<style>body{font-family:sans-serif;background:#f4f4f4;margin:0;padding:"
      "2em;}h2{color:#333;}ul{background:#fff;border-radius:8px;box-shadow:0 "
      "2px 8px #0001;max-width:500px;margin:auto;padding:1em;}li{margin:0.5em "
      "0;}a{color:#1976d2;text-decoration:none;font-weight:bold;}a:hover{text-"
      "decoration:underline;}</style></head><body>";
  html += "<h2>ESPWiFi Files</h2><ul>";
  File file = root.openNextFile();
  while (file) {
    String fname = String(file.name());
    html +=
        "<li><a href='" + fname + "' target='_blank'>" + fname + "</a></li>";
    file = root.openNextFile();
  }
  html += "</ul></body></html>";
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(200, "text/html", html);
};

#endif  // ESPWIFI_WEBSERVER_H