#ifndef ESPWiFi_WEB_SERVER_H
#define ESPWiFi_WEB_SERVER_H

#include "ESPWiFi.h"

// Helper: Add CORS headers to a response
void ESPWiFi::addCORS(AsyncWebServerResponse *response) {
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods",
                      "GET, POST, OPTIONS, PUT, DELETE");
  response->addHeader("Access-Control-Allow-Headers",
                      "Content-Type, Authorization");
}

void ESPWiFi::handleCorsPreflight(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(204);
  addCORS(response);
  request->send(response);
}

void ESPWiFi::sendJsonResponse(AsyncWebServerRequest *request, int statusCode,
                               const String &jsonBody) {
  AsyncWebServerResponse *response =
      request->beginResponse(statusCode, "application/json", jsonBody);
  addCORS(response);
  request->send(response);
}

void ESPWiFi::initWebServer() {
  // Only create web server if WiFi is enabled (no point without network access)
  if (!webServer && config["wifi"]["enabled"].as<bool>()) {
    webServer = new AsyncWebServer(80);
  }
}

void ESPWiFi::startWebServer() {
  if (webServerStarted || !config["wifi"]["enabled"].as<bool>()) {
    log(INFO, "🗄️  HTTP Web Server Disabled");
    return;
  }
  initWebServer();
  srvAll();
  webServer->begin();
  webServerStarted = true;
  log(INFO, "🗄️  HTTP Web Server Started:");
  String serverIP = WiFi.isConnected() ? WiFi.localIP().toString()
                                       : WiFi.softAPIP().toString();
  log(DEBUG, "\tURL: http://%s", serverIP.c_str());
  log(DEBUG, "\tURL: http://%s.local", config["deviceName"].as<String>().c_str());
}

void ESPWiFi::srvRoot() {
  initWebServer();
  // Serve index.html at root - no auth required (login page needs to load)
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

  // webServer->on("/foo", HTTP_GET, [this](AsyncWebServerRequest *request) {
  //   if (LittleFS.exists("/index.html")) {
  //     AsyncWebServerResponse *response =
  //         request->beginResponse(200, "text/plain", "BAR!");
  //     addCORS(response);
  //     request->send(response);
  //   }
  // });
}

void ESPWiFi::srvRestart() {
  initWebServer();
  webServer->on("/restart", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
      return;
    }
    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/plain", "Restarting...");
    addCORS(response);
    request->send(response);
    delay(1000);
    ESP.restart();
  });
}

void ESPWiFi::srvInfo() {
  initWebServer();
  // Device info endpoint
  webServer->on("/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
      return;
    }
    JsonDocument jsonDoc;
    jsonDoc["uptime"] = millis() / 1000;
    jsonDoc["ip"] = WiFi.isConnected() ? WiFi.localIP().toString()
                                       : WiFi.softAPIP().toString();
    jsonDoc["mac"] = WiFi.macAddress();

    // Construct AP SSID from config the same way as when starting AP
    String hostname = String(WiFi.getHostname());
    jsonDoc["hostname"] = hostname;
    jsonDoc["ap_ssid"] =
        config["wifi"]["ap"]["ssid"].as<String>() + "-" + hostname;
    jsonDoc["mdns"] = config["deviceName"].as<String>() + ".local";
    jsonDoc["chip"] = String(ESP.getChipModel());
    jsonDoc["sdk_version"] = String(ESP.getSdkVersion());
    jsonDoc["free_heap"] = ESP.getFreeHeap();
    jsonDoc["total_heap"] = ESP.getHeapSize();
    jsonDoc["used_heap"] = ESP.getHeapSize() - ESP.getFreeHeap();

    if (WiFi.isConnected()) {
      jsonDoc["client_ssid"] = WiFi.SSID();
      jsonDoc["rssi"] = WiFi.RSSI();
    }

    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    jsonDoc["littlefs_free"] = totalBytes - usedBytes;
    jsonDoc["littlefs_used"] = usedBytes;
    jsonDoc["littlefs_total"] = totalBytes;

    // Add SD card storage information if available
    if (sdCardInitialized && sd) {
      size_t sdTotalBytes, sdUsedBytes, sdFreeBytes;
      getStorageInfo("sd", sdTotalBytes, sdUsedBytes, sdFreeBytes);
      jsonDoc["sd_free"] = sdFreeBytes;
      jsonDoc["sd_used"] = sdUsedBytes;
      jsonDoc["sd_total"] = sdTotalBytes;
    }

    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", jsonResponse);
    addCORS(response);
    request->send(response);
  });
}

void ESPWiFi::srvAll() {
  srvAuth(); // Auth endpoints must be registered first
  srvRoot();
  srvOTA();
  srvInfo();
  srvGPIO();
  srvFiles();
  srvConfig();
  srvBluetooth();
  srvLog();
  srvRestart();
}

#endif // ESPWiFi_WEB_SERVER_H