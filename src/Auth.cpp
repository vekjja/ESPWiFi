// Auth.cpp
#include "ESPWiFi.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include <iomanip>
#include <sstream>
#include <vector>

std::string ESPWiFi::generateToken() {
  // Generate a simple token from MAC address + timestamp
  // In production, you might want a more secure token generation
  uint8_t mac[6];
  esp_err_t mac_ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (mac_ret != ESP_OK) {
    // Fallback: read MAC directly from hardware
    mac_ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  }

  std::string macStr;
  if (mac_ret == ESP_OK) {
    // Format MAC without colons
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 6; i++) {
      ss << std::setw(2) << (int)mac[i];
    }
    macStr = ss.str();
  } else {
    macStr = "000000000000";
  }

  unsigned long now = millis();
  std::stringstream tokenStream;
  tokenStream << macStr << std::hex << now;
  return tokenStream.str();
}

bool ESPWiFi::authEnabled() { return config["auth"]["enabled"].as<bool>(); }

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
        ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
        if (espwifi->verify(req, false) != ESP_OK) {
          return ESP_OK; // Response already sent (OPTIONS or error)
        }
        {
          ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

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
            espwifi->sendJsonResponse(req, 400, "{\"error\":\"Invalid JSON\"}");
            return ESP_OK;
          }

          std::string username = reqJson["username"].as<std::string>();
          std::string password = reqJson["password"].as<std::string>();

          // Check if auth is enabled
          if (!espwifi->authEnabled()) {
            espwifi->sendJsonResponse(
                req, 200, "{\"token\":\"\",\"message\":\"Auth disabled\"}");
            return ESP_OK;
          }

          // Verify username
          std::string expectedUsername =
              espwifi->config["auth"]["username"].as<std::string>();
          if (username != expectedUsername) {
            espwifi->sendJsonResponse(req, 401,
                                      "{\"error\":\"Invalid Credentials\"}");
            return ESP_OK;
          }

          // Verify password - check if password matches OR expectedPassword is
          // empty
          std::string expectedPassword =
              espwifi->config["auth"]["password"].as<std::string>();
          if (password != expectedPassword && expectedPassword.length() > 0) {
            espwifi->sendJsonResponse(req, 401,
                                      "{\"error\":\"Invalid Credentials\"}");
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
          espwifi->sendJsonResponse(req, 200, response);
          return ESP_OK;
        }
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &login_route);

  // Logout endpoint - invalidates token
  httpd_uri_t logout_route = {
      .uri = "/api/auth/logout",
      .method = HTTP_POST,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
        if (espwifi->verify(req, true) != ESP_OK) {
          return ESP_OK; // Response already sent (OPTIONS or error)
        }
        // Invalidate token by generating a new one
        std::string newToken = espwifi->generateToken();
        espwifi->config["auth"]["token"] = newToken;
        espwifi->saveConfig();
        espwifi->sendJsonResponse(req, 200, "{\"message\":\"Logged out\"}");
        return ESP_OK;
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &logout_route);
}

bool ESPWiFi::authorized(httpd_req_t *req) {
  // Get request information for logging
  const char *method = getMethodString(req->method);
  std::string url(req->uri);
  std::string userAgent = "-";
  std::string uri = req->uri;

  // List of allowed path patterns (supports * wildcard)
  static const std::vector<std::string> allowedPatterns = {
      "/api/auth/*", "/index.html", "/favicon.ico",
      "/static/*",   "/*.css",      "/*.js",
  };

  // Check if URI matches any allowed pattern
  for (const auto &pattern : allowedPatterns) {
    if (matchPattern(uri, pattern)) {
      return true;
    }
  }

  // Get User-Agent header if available
  size_t user_agent_len = httpd_req_get_hdr_value_len(req, "User-Agent");
  if (user_agent_len > 0) {
    char *user_agent = (char *)malloc(user_agent_len + 1);
    if (user_agent != nullptr) {
      httpd_req_get_hdr_value_str(req, "User-Agent", user_agent,
                                  user_agent_len + 1);
      userAgent = std::string(user_agent);
      free(user_agent);
    }
  }

  std::string clientInfo =
      std::string(method) + " - " + url + " \"" + userAgent + "\"";

  if (!authEnabled()) {
    log(ACCESS, "🔓 Auth disabled - %s", clientInfo.c_str());
    return true; // Auth disabled, allow all
  }

  // Check for Authorization header
  size_t auth_hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (auth_hdr_len == 0) {
    log(ACCESS, "🔒 401 Unauthorized (no auth header) - %s",
        clientInfo.c_str());
    return false;
  }

  char *auth_hdr = (char *)malloc(auth_hdr_len + 1);
  if (auth_hdr == nullptr) {
    log(ACCESS, "🔒 401 Unauthorized (memory error) - %s", clientInfo.c_str());
    return false;
  }

  httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, auth_hdr_len + 1);

  // Check if it's a Bearer token
  std::string auth_str(auth_hdr);
  free(auth_hdr);

  if (auth_str.find("Bearer ") != 0) {
    log(ACCESS, "🔒 401 Unauthorized (invalid auth format) - %s",
        clientInfo.c_str());
    return false;
  }

  // Extract token
  std::string token = auth_str.substr(7); // Remove "Bearer "
  std::string expectedToken = config["auth"]["token"].as<std::string>();

  // Compare tokens
  bool isAuthorized = token == expectedToken && expectedToken.length() > 0;

  if (isAuthorized) {
    log(ACCESS, "🔐 200 Authorized - %s", clientInfo.c_str());
  } else {
    log(ACCESS, "🔒 401 Unauthorized (invalid token) - %s", clientInfo.c_str());
  }

  return isAuthorized;
}