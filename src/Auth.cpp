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

bool ESPWiFi::authorized(httpd_req_t *req) {

  if (!authEnabled()) {
    return true; // Auth disabled, allow all
  }

  if (isExcludedPath(req->uri)) {
    return true; // Path is excluded, allow
  }

  const std::string expectedToken = config["auth"]["token"].as<std::string>();
  if (expectedToken.empty()) {
    return false;
  }

  // Browser navigations (window.open, <img>, <audio>, etc.) cannot attach custom
  // Authorization headers. Allow passing the bearer token via `?token=...`.
  // This is also used by WebSocket endpoints for auth.
  const std::string tokenParam = getQueryParam(req, "token");
  if (!tokenParam.empty() && tokenParam == expectedToken) {
    return true;
  }

  // Check for Authorization header
  size_t auth_hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
  if (auth_hdr_len == 0) {
    return false;
  }

  // Get Authorization header
  char *auth_hdr = (char *)malloc(auth_hdr_len + 1);
  if (auth_hdr == nullptr) {
    return false;
  }

  // Get Authorization header string
  httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, auth_hdr_len + 1);
  std::string auth_str(auth_hdr);
  free(auth_hdr);

  // Check if it's a Bearer token
  if (auth_str.find("Bearer ") != 0) {
    return false;
  }

  // Extract token
  std::string token = auth_str.substr(7); // Remove "Bearer "

  // Compare tokens
  return token == expectedToken;
}

esp_err_t ESPWiFi::verifyRequest(httpd_req_t *req, std::string *outClientInfo) {
  if (req == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  // Handle OPTIONS requests automatically (CORS preflight)
  if (req->method == HTTP_OPTIONS) {
    handleCorsPreflight(req);
    return ESP_ERR_HTTPD_RESP_SEND;
  }

  // Add CORS headers to all responses
  addCORS(req);

  // Capture early; slow/streaming sends may lose socket/headers if client
  // resets.
  std::string clientInfo;
  if (outClientInfo != nullptr) {
    clientInfo = getClientInfo(req);
  }

  // Check if authorized
  if (!authorized(req)) {
    if (clientInfo.empty()) {
      clientInfo = getClientInfo(req);
    }
    (void)sendJsonResponse(req, 401, "{\"error\":\"Unauthorized\"}",
                           &clientInfo);
    return ESP_ERR_HTTPD_INVALID_REQ; // Don't continue with handler
  }

  // If the request targets a protected file via the file APIs, treat it as an
  // invalid request (even if the token is valid).
  //
  // Note: some file operations (e.g. mkdir JSON body, upload multipart
  // filename) cannot be determined here and must still be enforced inside the
  // handler.
  auto normalizeApiPath = [](std::string p) -> std::string {
    if (p.empty()) {
      return std::string("/");
    }
    if (p.front() != '/') {
      p.insert(p.begin(), '/');
    }
    while (p.size() > 1 && p.back() == '/') {
      p.pop_back();
    }
    return p;
  };

  const char *uri = req->uri;
  if (uri != nullptr) {
    std::string_view full(uri);
    size_t q = full.find('?');
    std::string_view pathOnly =
        (q == std::string_view::npos) ? full : full.substr(0, q);

    // Block file delete/rename targets early (query-param based).
    if (pathOnly == "/api/files/delete") {
      std::string fsParam = getQueryParam(req, "fs");
      std::string filePath = normalizeApiPath(getQueryParam(req, "path"));
      if (!fsParam.empty() && !filePath.empty() &&
          isProtectedFile(fsParam, filePath)) {
        if (clientInfo.empty()) {
          clientInfo = getClientInfo(req);
        }
        (void)sendJsonResponse(req, 403, "{\"error\":\"Path is protected\"}",
                               &clientInfo);
        return ESP_ERR_HTTPD_INVALID_REQ;
      }
    } else if (pathOnly == "/api/files/rename") {
      std::string fsParam = getQueryParam(req, "fs");
      std::string oldPath = normalizeApiPath(getQueryParam(req, "oldPath"));
      if (!fsParam.empty() && !oldPath.empty() &&
          isProtectedFile(fsParam, oldPath)) {
        if (clientInfo.empty()) {
          clientInfo = getClientInfo(req);
        }
        (void)sendJsonResponse(req, 403, "{\"error\":\"Path is protected\"}",
                               &clientInfo);
        return ESP_ERR_HTTPD_INVALID_REQ;
      }
    }
  }

  if (outClientInfo != nullptr) {
    *outClientInfo = std::move(clientInfo);
  }
  return ESP_OK; // Verification passed, continue with handler
}

#endif // ESPWiFi_AUTH