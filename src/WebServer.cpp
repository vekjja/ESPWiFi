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
    Serial.println("Restarting...");
    delay(1000);
    ESP.restart();
  });

  // Handle file listing
  webServer.on("/list-files", HTTP_GET, [this]() { listFilesHandler(); });

  // Start the server
  webServer.begin();
};

void ESPWiFi::listFilesHandler() {
  String path = "/";
  File root = LittleFS.open(path, "r");
  String fileNames = "[";  // Start JSON array
  File file = root.openNextFile();
  while (file) {
    if (fileNames.length() > 1) {
      fileNames += ",";
    }
    fileNames += "\"" + String(file.name()) + "\"";
    file = root.openNextFile();
  }
  fileNames += "]";  // Close JSON array
  webServer.sendHeader("Access-Control-Allow-Origin", "*");
  webServer.send(200, "application/json", fileNames);  // Send JSON response
};

#endif  // ESPWIFI_WEBSERVER_H