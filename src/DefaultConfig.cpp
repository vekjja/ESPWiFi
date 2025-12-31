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
  doc["wifi"]["accessPoint"]["ssid"] = doc["deviceName"].as<std::string>();
  doc["wifi"]["accessPoint"]["password"] = "espwifi!";

  // WiFi Client
  doc["wifi"]["client"]["ssid"] = "";
  doc["wifi"]["client"]["password"] = "";

  // Bluetooth (BLE - works on ESP32-S3 and ESP32-C3)
  doc["bluetooth"]["enabled"] = false;

  // Logging: verbose, access, debug, info, warning, error
  doc["log"]["file"] = "/log";
  doc["log"]["enabled"] = true;
  doc["log"]["level"] = "debug";
  doc["log"]["useSD"] = true;

  // Auth
  doc["auth"]["enabled"] = false;
  doc["auth"]["password"] = "admin";
  doc["auth"]["username"] = "admin";
  // CORS (auth.cors)
  // - enabled: controls whether CORS headers are emitted
  // - origins: allowed Origin patterns (supports '*' and '?')
  // - methods: allowed methods for preflight
  // - paths: optional URI path patterns to apply CORS to (supports '*' and '?')
  doc["auth"]["cors"]["enabled"] = true;
  JsonArray corsOrigins = doc["auth"]["cors"]["origins"].to<JsonArray>();
  corsOrigins.add("*");
  JsonArray corsMethods = doc["auth"]["cors"]["methods"].to<JsonArray>();
  corsMethods.add("GET");
  corsMethods.add("POST");
  corsMethods.add("PUT");
  corsMethods.add("DELETE");
  JsonArray excludePaths = doc["auth"]["excludePaths"].to<JsonArray>();
  excludePaths.add("/");
  excludePaths.add("/static/*");
  excludePaths.add("/favicon.ico");
  excludePaths.add("/api/auth/login");
  excludePaths.add("/asset-manifest.json");

  // Paths that are always protected from HTTP file APIs (even when authorized).
  // Patterns support '*' and '?' (same matcher as excludePaths).
  JsonArray protectFiles = doc["auth"]["protectFiles"].to<JsonArray>();
  protectFiles.add("/log");
  protectFiles.add("/static/*");
  protectFiles.add("/index.html");
  protectFiles.add("/config.json");
  protectFiles.add("/asset-manifest.json");

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

  // OTA - based on LittleFS partition
  doc["ota"]["enabled"] = isOTAEnabled();

  return doc;
}

#endif // ESPWiFi_DEFAULT_CONFIG