// Auth.cpp
#ifndef ESPWiFi_SRV_AUTH
#define ESPWiFi_SRV_AUTH
#include "ESPWiFi.h"

void ESPWiFi::srvAuth() {
  // Login endpoint - no auth required
  registerRoute(
      "/api/auth/login", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Read request body
        size_t content_len = req->content_len;
        if (content_len > 512) {
          httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE,
                              "Request body too large");
          return ESP_FAIL;
        }

        char *content = (char *)malloc(content_len + 1);
        if (content == nullptr) {
          httpd_resp_send_500(req);
          return ESP_FAIL;
        }

        int ret = httpd_req_recv(req, content, content_len);
        if (ret <= 0) {
          free(content);
          if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
          }
          return ESP_FAIL;
        }

        content[content_len] = '\0';
        std::string json_body(content);
        free(content);

        // Parse JSON request body
        JsonDocument reqJson;
        DeserializationError error = deserializeJson(reqJson, json_body);
        if (error) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid JSON\"}", &clientInfo);
          return ESP_OK;
        }

        std::string username = reqJson["username"].as<std::string>();
        std::string password = reqJson["password"].as<std::string>();

        // Check if auth is enabled
        if (!espwifi->authEnabled()) {
          (void)espwifi->sendJsonResponse(
              req, 200, "{\"token\":\"\",\"message\":\"Auth disabled\"}",
              &clientInfo);
          return ESP_OK;
        }

        // Verify username
        std::string expectedUsername =
            espwifi->config["auth"]["username"].as<std::string>();
        if (username != expectedUsername) {
          (void)espwifi->sendJsonResponse(
              req, 401, "{\"error\":\"Invalid Credentials\"}", &clientInfo);
          return ESP_OK;
        }

        // Verify password - check if password matches OR expectedPassword is
        // empty
        std::string expectedPassword =
            espwifi->config["auth"]["password"].as<std::string>();
        if (password != expectedPassword && expectedPassword.length() > 0) {
          (void)espwifi->sendJsonResponse(
              req, 401, "{\"error\":\"Invalid Credentials\"}", &clientInfo);
          return ESP_OK;
        }

        // Generate or get existing token
        std::string token = espwifi->config["auth"]["token"].as<std::string>();
        if (token.length() == 0) {
          token = espwifi->generateToken();
          espwifi->config["auth"]["token"] = token;
          espwifi->saveConfig();
        }

        std::string response = "{\"token\":\"" + token + "\"}";
        (void)espwifi->sendJsonResponse(req, 200, response, &clientInfo);
        return ESP_OK;
      });

  // Logout endpoint - invalidates token
  registerRoute("/api/auth/logout", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  // Invalidate token by generating a new one
                  std::string newToken = espwifi->generateToken();
                  espwifi->config["auth"]["token"] = newToken;
                  espwifi->saveConfig();
                  (void)espwifi->sendJsonResponse(
                      req, 200, "{\"message\":\"Logged out\"}", &clientInfo);
                  return ESP_OK;
                });
}

#endif // ESPWiFi_SRV_AUTH
