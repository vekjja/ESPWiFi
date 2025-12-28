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

bool ESPWiFi::authorized(httpd_req_t *req) {

  if (!authEnabled()) {
    return true; // Auth disabled, allow all
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
  std::string expectedToken = config["auth"]["token"].as<std::string>();

  // Compare tokens
  return token == expectedToken && expectedToken.length() > 0;
}

#endif // ESPWiFi_AUTH