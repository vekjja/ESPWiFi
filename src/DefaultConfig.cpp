#ifndef ESPWiFi_DEFAULT_CONFIG
#define ESPWiFi_DEFAULT_CONFIG

#include "ESPWiFi.h"

JsonDocument ESPWiFi::defaultConfig() {
  JsonDocument doc;

  doc["deviceName"] = "ESPWiFi";
  doc["hostname"] = getHostname();

  doc["wifi"]["enabled"] = true;
  doc["wifi"]["mode"] = "accessPoint";

  // Access Point
  doc["wifi"]["accessPoint"]["ssid"] = doc["deviceName"];
  doc["wifi"]["accessPoint"]["password"] = "espwifi!";

  // WiFi Client
  doc["wifi"]["client"]["ssid"] = "";
  doc["wifi"]["client"]["password"] = "";
  // mDNS (Multicast DNS) - network service discovery
  doc["wifi"]["mdns"] = true;

  // Bluetooth Audio
  doc["bluetooth"]["enabled"] = false;

  // Logging: verbose, access, debug, info, warning, error
  doc["log"]["file"] = "/espwifi.log";
  doc["log"]["level"] = "debug";
  doc["log"]["enabled"] = true;
  doc["log"]["useSD"] = true;
  doc["log"]["maskedKeys"] = JsonArray();
  doc["log"]["maskedKeys"].add("password");
  doc["log"]["maskedKeys"].add("token");

  // Auth
  // - enabled: controls whether authentication is enabled
  doc["auth"]["enabled"] = false;
  // - password: password for authentication
  doc["auth"]["password"] = "admin";
  // - username: username for authentication
  doc["auth"]["username"] = "admin";

  // CORS (auth.cors)
  // - enabled: controls whether CORS headers are emitted
  doc["auth"]["cors"]["enabled"] = true;
  // - origins: allowed Origin patterns (supports '*' and '?')
  doc["auth"]["cors"]["origins"].add("*");
  // - methods: allowed methods for preflight
  doc["auth"]["cors"]["methods"].add("GET");
  doc["auth"]["cors"]["methods"].add("POST");
  doc["auth"]["cors"]["methods"].add("PUT");
  // Paths that are excluded from authentication (supports '*' and '?')
  doc["auth"]["excludePaths"].add("/");
  doc["auth"]["excludePaths"].add("/static/*");
  doc["auth"]["excludePaths"].add("/favicon.ico");
  doc["auth"]["excludePaths"].add("/api/auth/login");
  doc["auth"]["excludePaths"].add("/asset-manifest.json");
  // Paths that are always protected (supports '*' and '?')
  doc["auth"]["protectFiles"].add("/static/*");
  doc["auth"]["protectFiles"].add("/index.html");
  doc["auth"]["protectFiles"].add("/config.json");
  doc["auth"]["protectFiles"].add("/asset-manifest.json");

  doc["ota"]["enabled"] = isOTAEnabled();

  // Camera
  // #ifdef ESPWiFi_CAMERA
  //   doc["camera"]["installed"] = true;
  //   doc["camera"]["enabled"] = false;
  //   doc["camera"]["frameRate"] = 10;
  //   doc["camera"]["rotation"] = 0;
  //   doc["camera"]["brightness"] = 1;
  //   doc["camera"]["contrast"] = 1;
  //   doc["camera"]["saturation"] = 1;
  //   doc["camera"]["exposure_level"] = 1;
  //   doc["camera"]["exposure_value"] = 400;
  //   doc["camera"]["agc_gain"] = 2;
  //   doc["camera"]["gain_ceiling"] = 2;
  //   doc["camera"]["white_balance"] = 1;
  //   doc["camera"]["awb_gain"] = 1;
  //   doc["camera"]["wb_mode"] = 0;
  // #else
  //   doc["camera"]["installed"] = false;
  // #endif

  return doc;
}

#endif // ESPWiFi_DEFAULT_CONFIG