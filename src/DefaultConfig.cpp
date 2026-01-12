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
  doc["wifi"]["accessPoint"]["ssid"] = genHostname();
  doc["wifi"]["accessPoint"]["password"] = "espw!f!!";

  // WiFi Client
  doc["wifi"]["client"]["ssid"] = "";
  doc["wifi"]["client"]["password"] = "";

  // WiFi Power Management
  // - txPower: WiFi transmit power in dBm (range: 2-20, default: 19.5)
  //   Common values: 13, 15, 17, 18, 19.5, 20
  //   Lower values reduce power consumption and interference
  doc["wifi"]["power"]["txPower"] = 19.5;
  // - powerSave: WiFi power save mode (none, min, max)
  //   - "none": No power saving (best performance, highest power consumption)
  //   - "min": Minimum modem power saving (balanced)
  //   - "max": Maximum modem power saving (lowest power, affects performance)
  doc["wifi"]["power"]["powerSave"] = "none";

  // mDNS (Multicast DNS) - network service discovery
  doc["wifi"]["mdns"] = true;

  // Bluetooth Audio
  doc["bluetooth"]["enabled"] = false;

  // BLE Provisioning
  doc["ble"]["enabled"] = true;
  doc["ble"]["passkey"] = 123456;

  // Logging: verbose, access, debug, info, warning, error
  doc["log"]["file"] = "/espwifi.log";
  doc["log"]["level"] = "debug";
  doc["log"]["enabled"] = true;
  doc["log"]["useSD"] = true;
  doc["log"]["maskedKeys"] = JsonArray();
  doc["log"]["maskedKeys"].add("password");
  doc["log"]["maskedKeys"].add("passkey");
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
  doc["auth"]["protectedFiles"].add("/static/*");
  doc["auth"]["protectedFiles"].add("/index.html");
  doc["auth"]["protectedFiles"].add("/config.json");
  doc["auth"]["protectedFiles"].add("/asset-manifest.json");

// SD Card
#if ESPWiFi_HAS_SDCARD
  doc["sd"]["installed"] = true;
#else
  doc["sd"]["installed"] = false;
#endif
  doc["sd"]["initialized"] = false;

// Camera
#if ESPWiFi_HAS_CAMERA
  doc["camera"]["installed"] = true;
  doc["camera"]["frameRate"] = 10;
  doc["camera"]["rotation"] = 0;
  doc["camera"]["brightness"] = 0;
  doc["camera"]["contrast"] = 0;
  doc["camera"]["saturation"] = 0;
  doc["camera"]["sharpness"] = 0;
  doc["camera"]["denoise"] = 0;
  doc["camera"]["quality"] = 12;
  doc["camera"]["exposure_level"] = 1;
  doc["camera"]["exposure_value"] = 360;
  doc["camera"]["agc_gain"] = 2;
  doc["camera"]["gain_ceiling"] = 2;
  doc["camera"]["white_balance"] = 1;
  doc["camera"]["awb_gain"] = 1;
  doc["camera"]["wb_mode"] = 0;
#else
  doc["camera"]["installed"] = false;
#endif

  doc["ota"]["enabled"] = isOTAEnabled();
  return doc;
}

#endif // ESPWiFi_DEFAULT_CONFIG