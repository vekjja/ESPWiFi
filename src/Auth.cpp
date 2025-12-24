#ifndef ESPWiFi_AUTH_H
#define ESPWiFi_AUTH_H

#include "ESPWiFi.h"

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

  String clientInfo =
      clientIP + " " + method + " - " + url + " \"" + userAgent + "\" ";

  if (!authEnabled()) {
    log(ACCESS, "🔓 Auth disabled - %s", clientInfo.c_str());
    return true; // Auth disabled, allow all
  }

  // Check for Authorization header
  if (!request->hasHeader("Authorization")) {
    log(ACCESS, "🔒 401 Unauthorized (no auth header) - %s",
        clientInfo.c_str());
    return false;
  }

  const AsyncWebHeader *authHeader = request->getHeader("Authorization");
  String authValue = authHeader->value();

  // Check if it's a Bearer token
  if (!authValue.startsWith("Bearer ")) {
    log(ACCESS, "🔒 401 Unauthorized (invalid auth format) - %s",
        clientInfo.c_str());
    return false;
  }

  // Extract token
  String token = authValue.substring(7); // Remove "Bearer "
  String expectedToken = config["auth"]["token"].as<String>();

  // Compare tokens
  bool isAuthorized = token == expectedToken && expectedToken.length() > 0;

  if (isAuthorized) {
    log(ACCESS, "🔐 200 Authorized - %s", clientInfo.c_str());
  } else {
    log(ACCESS, "🔒 401 Unauthorized (invalid token) - %s", clientInfo.c_str());
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
          sendJsonResponse(request, 401, "{\"error\":\"Invalid Credentials\"}");
          return;
        }

        // Verify password - check if password matches OR expectedPassword
        // is empty
        String expectedPassword = config["auth"]["password"].as<String>();
        if (password != expectedPassword && expectedPassword.length() > 0) {
          sendJsonResponse(request, 401, "{\"error\":\"Invalid Credentials\"}");
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

#endif // ESPWiFi_AUTH_H
