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
  log("🗄️ HTTP Web Server Started:");
  logf("\tURL: http://%s\n", WiFi.localIP().toString().c_str());
  logf("\tURL: http://%s.local\n", config["mdns"].as<String>().c_str());
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
    jsonDoc["ip"] = WiFi.localIP().toString();
    jsonDoc["mac"] = WiFi.macAddress();

    // Construct AP SSID from config the same way as when starting AP
    String hostname = String(WiFi.getHostname());
    jsonDoc["hostname"] = hostname;
    jsonDoc["ap_ssid"] = config["ap"]["ssid"].as<String>() + "-" + hostname;
    jsonDoc["mdns"] = config["mdns"].as<String>() + ".local";
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

bool ESPWiFi::authEnabled() { return config["auth"]["enabled"].as<bool>(); }

String ESPWiFi::generateToken() {
  // Generate a simple token from MAC address + timestamp
  // In production, you might want a more secure token generation
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  unsigned long now = millis();
  return mac + String(now, HEX);
}

bool ESPWiFi::authorized(AsyncWebServerRequest *request) {
  // Get request information for logging
  String method = request->methodToString();
  String url = request->url();
  String clientIP =
      request->client() ? request->client()->remoteIP().toString() : "unknown";
  String userAgent = request->hasHeader("User-Agent")
                         ? request->getHeader("User-Agent")->value()
                         : "-";

  if (!authEnabled()) {
    // Log access even when auth is disabled
    logf("🌐 [ACCESS] %s %s - %s \"%s\" \"%s\" - Auth disabled\n",
         clientIP.c_str(), method.c_str(), url.c_str(), userAgent.c_str(),
         "200 OK");
    return true; // Auth disabled, allow all
  }

  // Check for Authorization header
  if (!request->hasHeader("Authorization")) {
    logf("🔒 [ACCESS] %s %s - %s \"%s\" \"%s\" - 401 Unauthorized (no auth "
         "header)\n",
         clientIP.c_str(), method.c_str(), url.c_str(), userAgent.c_str(),
         "401 Unauthorized");
    return false;
  }

  const AsyncWebHeader *authHeader = request->getHeader("Authorization");
  String authValue = authHeader->value();

  // Check if it's a Bearer token
  if (!authValue.startsWith("Bearer ")) {
    logf("🔒 [ACCESS] %s %s - %s \"%s\" \"%s\" - 401 Unauthorized (invalid "
         "auth format)\n",
         clientIP.c_str(), method.c_str(), url.c_str(), userAgent.c_str(),
         "401 Unauthorized");
    return false;
  }

  // Extract token
  String token = authValue.substring(7); // Remove "Bearer "
  String expectedToken = config["auth"]["token"].as<String>();

  // Compare tokens
  bool isAuthorized = token == expectedToken && expectedToken.length() > 0;

  if (isAuthorized) {
    logf("✅ [ACCESS] %s %s - %s \"%s\" \"%s\" - 200 Authorized\n",
         clientIP.c_str(), method.c_str(), url.c_str(), userAgent.c_str(),
         "200 OK");
  } else {
    logf("🔒 [ACCESS] %s %s - %s \"%s\" \"%s\" - 401 Unauthorized (invalid "
         "token)\n",
         clientIP.c_str(), method.c_str(), url.c_str(), userAgent.c_str(),
         "401 Unauthorized");
  }

  return isAuthorized;
}

void ESPWiFi::srvAuth() {
  initWebServer();

  // Login endpoint - no auth required
  webServer->on(
      "/api/auth/login", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/api/auth/login",
      [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject reqJson = json.as<JsonObject>();
        String username = reqJson["username"] | "";
        String password = reqJson["password"] | "";

        // Check if auth is enabled
        if (!authEnabled()) {
          sendJsonResponse(request, 200,
                           "{\"token\":\"\",\"message\":\"Auth disabled\"}");
          return;
        }

        // Verify username
        String expectedUsername = config["auth"]["username"].as<String>();
        if (username != expectedUsername) {
          sendJsonResponse(request, 401, "{\"error\":\"Invalid username\"}");
          return;
        }

        // Verify password - check if password matches OR expectedPassword
        // is empty
        String expectedPassword = config["auth"]["password"].as<String>();
        if (password != expectedPassword && expectedPassword.length() > 0) {
          sendJsonResponse(request, 401, "{\"error\":\"Invalid password\"}");
          return;
        }

        // Generate or get existing token
        String token = config["auth"]["token"].as<String>();
        if (token.length() == 0) {
          token = generateToken();
          config["auth"]["token"] = token;
          saveConfig();
        }

        String response = "{\"token\":\"" + token + "\"}";
        sendJsonResponse(request, 200, response);
      }));

  // Logout endpoint - invalidates token
  webServer->on(
      "/api/auth/logout", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
          handleCorsPreflight(request);
          return;
        }

        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        // Invalidate token by generating a new one
        String newToken = generateToken();
        config["auth"]["token"] = newToken;
        saveConfig();

        sendJsonResponse(request, 200, "{\"message\":\"Logged out\"}");
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
  srvRestart();
}

#endif // ESPWiFi_WEB_SERVER_H