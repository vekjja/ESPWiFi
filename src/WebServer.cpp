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
        "padding:2em;}h2{color:#333;}table{background:#fff;border-radius:8px;"
        "box-shadow:0 2px 8px "
        "#0001;border-collapse:collapse;width:100%;max-width:500px;margin:auto;"
        "}th,td{padding:10px "
        "16px;text-align:left;}th{background:#f0f0f0;}tr:nth-child(even){"
        "background:#fafafa;}tr:hover{background:#e0f7fa;}</style></"
        "head><body>";
    info += "<h2>ESPWiFi Device Info</h2><table>";
    info += "<tr><th>Property</th><th>Value</th></tr>";
    info += "<tr><td>MAC Address</td><td>" + WiFi.macAddress() + "</td></tr>";
    info += "<tr><td>IP Address</td><td>" + WiFi.localIP().toString() +
            "</td></tr>";
    info += "<tr><td>Chip Model</td><td>" + String(ESP.getChipModel()) +
            "</td></tr>";
    info += "<tr><td>Chip Revision</td><td>" + String(ESP.getChipRevision()) +
            "</td></tr>";
    info += "<tr><td>CPU Frequency</td><td>" + String(ESP.getCpuFreqMHz()) +
            " MHz</td></tr>";
    info += "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap()) +
            " bytes</td></tr>";
    info += "<tr><td>Free PSRAM</td><td>" + String(ESP.getFreePsram()) +
            " bytes</td></tr>";
    info += "<tr><td>Sketch Size</td><td>" + String(ESP.getSketchSize()) +
            " bytes</td></tr>";
    info += "<tr><td>Free Sketch Space</td><td>" +
            String(ESP.getFreeSketchSpace()) + " bytes</td></tr>";
    info += "<tr><td>Sketch MD5</td><td>" + ESP.getSketchMD5() + "</td></tr>";
    info += "<tr><td>Flash Chip Size</td><td>" +
            String(ESP.getFlashChipSize()) + " bytes</td></tr>";
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
    info += "</table></body></html>";
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