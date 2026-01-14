// Auth.cpp
#ifndef ESPWiFi_AUTH
#define ESPWiFi_AUTH
#include "ESPWiFi.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <iomanip>
#include <sstream>
#include <vector>

bool ESPWiFi::authEnabled() { return config["auth"]["enabled"].as<bool>(); }

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

bool ESPWiFi::isExcludedPath(const char *uri) {
  if (uri == nullptr) {
    return false;
  }

  // Match only against the path (ignore query string) so web clients with
  // cache-busting params don't break auth exclusions.
  std::string_view full(uri);
  size_t q = full.find('?');
  std::string_view path =
      (q == std::string_view::npos) ? full : full.substr(0, q);

  JsonVariant excludes = config["auth"]["excludePaths"];
  if (!excludes.is<JsonArray>()) {
    return false;
  }

  // Iterate without copying into a std::vector (min RAM, no per-request heap).
  JsonArray arr = excludes.as<JsonArray>();
  for (JsonVariant v : arr) {
    const char *pat = v.as<const char *>();
    if (pat == nullptr || pat[0] == '\0') {
      continue;
    }

    std::string_view pattern(pat);

    // Special-case "/" so it matches ONLY the root path, not "everything that
    // contains a slash" (which would effectively disable auth).
    if (pattern == "/") {
      if (path == "/") {
        return true;
      }
      continue;
    }

    if (matchPattern(path, pattern)) {
      return true;
    }
  }

  return false;
}

bool ESPWiFi::authorized(PsychicRequest *request) {
  if (request == nullptr) {
    return false;
  }

  if (!authEnabled()) {
    return true; // Auth disabled, allow all
  }

  String uri = request->uri();
  if (isExcludedPath(uri.c_str())) {
    return true; // Path is excluded, allow
  }

  // Check for Authorization header
  String authHeader = request->header("Authorization");
  if (authHeader.length() == 0) {
    return false;
  }

  // Check if it's a Bearer token
  if (authHeader.indexOf("Bearer ") != 0) {
    return false;
  }

  // Extract token
  String token = authHeader.substring(7); // Remove "Bearer "
  std::string expectedToken = config["auth"]["token"].as<std::string>();

  // Compare tokens
  return (token.c_str() == expectedToken) && expectedToken.length() > 0;
}

esp_err_t ESPWiFi::verifyRequest(PsychicRequest *request,
                                 std::string *outClientInfo) {
  if (request == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  // Handle OPTIONS requests automatically (CORS preflight)
  if (request->method() == HTTP_OPTIONS) {
    request->response()->send(200);
    return ESP_OK;
  }

  // Add CORS headers to all responses
  addCORS(request);

  // Capture client info
  if (outClientInfo != nullptr) {
    *outClientInfo = getClientInfo(request);
  }

  // Check if authorized
  if (!authorized(request)) {
    std::string clientInfo = getClientInfo(request);
    sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
    return ESP_ERR_HTTPD_INVALID_REQ; // Don't continue with handler
  }

  return ESP_OK;
}

void ESPWiFi::addCORS(PsychicRequest *request) {
  if (request == nullptr) {
    return;
  }

  PsychicResponse *response = request->response();
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods",
                      "GET, POST, PUT, DELETE, PATCH, OPTIONS");
  response->addHeader("Access-Control-Allow-Headers",
                      "Content-Type, Authorization");
  response->addHeader("Access-Control-Max-Age", "3600");
}

#endif // ESPWiFi_AUTH