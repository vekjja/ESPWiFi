#include "ESPWiFi.h"
#include "esp_mac.h"

void ESPWiFi::startCloudMedia() {
  // Check if cloud client is enabled in config
  bool enabled = config["cloud"]["enabled"] | false;
  if (!enabled) {
    log(INFO, "☁️ Media cloud tunnel disabled (cloud not enabled)");
    return;
  }

  // Check if control tunnel is connected first
  if (!cloudCtl.isConnected()) {
    log(INFO,
        "☁️ Media cloud tunnel disabled (control tunnel not connected yet)");
    return;
  }

  // Check if media tunnel is explicitly enabled
  bool mediaTunnelEnabled =
      config["cloud"]["mediaEnabled"] | true; // Default true if cloud enabled
  if (!mediaTunnelEnabled) {
    log(INFO, "☁️ Media cloud tunnel disabled in config");
    return;
  }

  // Get configuration
  const char *baseUrl = config["cloud"]["baseUrl"];
  bool autoReconnect = config["cloud"]["autoReconnect"];
  uint32_t reconnectDelay = config["cloud"]["reconnectDelay"];

  // Use current hostname as device ID
  std::string hostname = config["hostname"].as<std::string>();
  if (hostname.empty()) {
    hostname = genHostname();
  }
  const char *deviceId = hostname.c_str();

  // Get auth token from device config
  const char *authToken = config["auth"]["token"] | nullptr;

  log(INFO, "☁️ Starting media cloud tunnel");
  log(INFO, "☁️ Base URL: %s", baseUrl);
  log(INFO, "☁️ Device ID: %s", deviceId);
  log(INFO, "☁️ Tunnel: ws_media");

  // Configure media cloud client
  CloudMedia::Config cfg;
  cfg.enabled = true;
  cfg.baseUrl = baseUrl;
  cfg.deviceId = deviceId;
  cfg.authToken = authToken;
  cfg.tunnel = "ws_media"; // Separate tunnel for media (camera, audio, etc)
  cfg.autoReconnect = autoReconnect;
  cfg.reconnectDelay = reconnectDelay;

  // No message handler needed for media tunnel - it only sends binary frames
  // Media frames will be sent via cloudMedia.sendBinary()

  // Initialize and connect
  if (!cloudMedia.begin(cfg)) {
    log(ERROR, "☁️ Failed to initialize media cloud tunnel");
    return;
  }

  log(INFO, "☁️ Media cloud tunnel started");
}
