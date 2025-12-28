// Auth.cpp
#ifndef ESPWiFi_SRV_AUTH
#define ESPWiFi_SRV_AUTH
#include "ESPWiFi.h"

void ESPWiFi::srvAuth() {
  if (!webServer) {
    log(ERROR, "Cannot start auth API /api/auth: web server not initialized");
    return;
  }

  // Login endpoint - no auth required
  httpd_uri_t login_route = {
      .uri = "/api/auth/login",
      .method = HTTP_POST,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWIFI_ROUTE_GUARD_NOAUTH(req, espwifi, clientInfo);
        {
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
            espwifi->sendJsonResponse(req, 400, "{\"error\":\"Invalid JSON\"}",
                                      &clientInfo);
            return ESP_OK;
          }

          std::string username = reqJson["username"].as<std::string>();
          std::string password = reqJson["password"].as<std::string>();

          // Check if auth is enabled
          if (!espwifi->authEnabled()) {
            espwifi->sendJsonResponse(
                req, 200, "{\"token\":\"\",\"message\":\"Auth disabled\"}",
                &clientInfo);
            return ESP_OK;
          }

          // Verify username
          std::string expectedUsername =
              espwifi->config["auth"]["username"].as<std::string>();
          if (username != expectedUsername) {
            espwifi->sendJsonResponse(
                req, 401, "{\"error\":\"Invalid Credentials\"}", &clientInfo);
            return ESP_OK;
          }

          // Verify password - check if password matches OR expectedPassword is
          // empty
          std::string expectedPassword =
              espwifi->config["auth"]["password"].as<std::string>();
          if (password != expectedPassword && expectedPassword.length() > 0) {
            espwifi->sendJsonResponse(
                req, 401, "{\"error\":\"Invalid Credentials\"}", &clientInfo);
            return ESP_OK;
          }

          // Generate or get existing token
          std::string token =
              espwifi->config["auth"]["token"].as<std::string>();
          if (token.length() == 0) {
            token = espwifi->generateToken();
            espwifi->config["auth"]["token"] = token;
            espwifi->saveConfig();
          }

          std::string response = "{\"token\":\"" + token + "\"}";
          espwifi->sendJsonResponse(req, 200, response, &clientInfo);
          return ESP_OK;
        }
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &login_route);

  // Logout endpoint - invalidates token
  httpd_uri_t logout_route = {.uri = "/api/auth/logout",
                              .method = HTTP_POST,
                              .handler = [](httpd_req_t *req) -> esp_err_t {
                                ESPWIFI_ROUTE_GUARD(req, espwifi, clientInfo);
                                // Invalidate token by generating a new one
                                std::string newToken = espwifi->generateToken();
                                espwifi->config["auth"]["token"] = newToken;
                                espwifi->saveConfig();
                                espwifi->sendJsonResponse(
                                    req, 200, "{\"message\":\"Logged out\"}",
                                    &clientInfo);
                                return ESP_OK;
                              },
                              .user_ctx = this};
  httpd_register_uri_handler(webServer, &logout_route);
}

#endif // ESPWiFi_SRV_AUTH