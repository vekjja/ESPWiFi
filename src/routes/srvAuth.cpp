// Auth.cpp
#ifndef ESPWiFi_SRV_AUTH
#define ESPWiFi_SRV_AUTH
#include "ESPWiFi.h"

void ESPWiFi::srvAuth() {
  // Login endpoint - no auth required
  registerRoute(
      "/api/auth/login", HTTP_POST,
      [this](PsychicRequest *request) -> esp_err_t {
        // Parse JSON request body
        String body = request->body();
        JsonDocument reqJson;
        DeserializationError error = deserializeJson(reqJson, body.c_str());
        if (error) {
          sendJsonResponse(request, 400, "{\"error\":\"Invalid JSON\"}");
          return ESP_OK;
        }

        std::string username = reqJson["username"].as<std::string>();
        std::string password = reqJson["password"].as<std::string>();

        // Check if auth is enabled
        if (!authEnabled()) {
          sendJsonResponse(request, 200,
                           "{\"token\":\"\",\"message\":\"Auth disabled\"}");
          return ESP_OK;
        }

        // Verify username
        std::string expectedUsername =
            config["auth"]["username"].as<std::string>();
        if (username != expectedUsername) {
          sendJsonResponse(request, 401, "{\"error\":\"Invalid Credentials\"}");
          return ESP_OK;
        }

        // Verify password
        std::string expectedPassword =
            config["auth"]["password"].as<std::string>();
        if (password != expectedPassword && expectedPassword.length() > 0) {
          sendJsonResponse(request, 401, "{\"error\":\"Invalid Credentials\"}");
          return ESP_OK;
        }

        // Generate or get existing token
        std::string token = config["auth"]["token"].as<std::string>();
        if (token.length() == 0) {
          token = generateToken();
          config["auth"]["token"] = token;
          saveConfig();
        }

        std::string response = "{\"token\":\"" + token + "\"}";
        sendJsonResponse(request, 200, response);
        return ESP_OK;
      });

  // Logout endpoint - invalidates token
  registerRoute("/api/auth/logout", HTTP_POST,
                [this](PsychicRequest *request) -> esp_err_t {
                  // Invalidate token by generating a new one
                  std::string newToken = generateToken();
                  config["auth"]["token"] = newToken;
                  saveConfig();
                  sendJsonResponse(request, 200,
                                   "{\"message\":\"Logged out\"}");
                  return ESP_OK;
                });
}

#endif // ESPWiFi_SRV_AUTH
