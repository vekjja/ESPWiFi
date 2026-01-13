#include "ESPWiFi.h"
#include "esp_mac.h"

void ESPWiFi::startCloud() {
  // Check if cloud client is enabled in config
  bool enabled = config["cloud"]["enabled"] | false;
  if (!enabled) {
    log(INFO, "☁️ Cloud client disabled");
    return;
  }

  // Get configuration
  const char *baseUrl = config["cloud"]["baseUrl"];
  const char *deviceId = config["cloud"]["deviceId"];
  const char *tunnel = config["cloud"]["tunnel"];
  bool autoReconnect = config["cloud"]["autoReconnect"];
  uint32_t reconnectDelay = config["cloud"]["reconnectDelay"];

  // Get auth token from device config (used to authenticate UI connections)
  const char *authToken = config["auth"]["token"] | nullptr;

  log(INFO, "☁️ Starting cloud client");
  log(INFO, "☁️ Base URL: %s", baseUrl);
  log(INFO, "☁️ Device ID: %s", deviceId);
  log(INFO, "☁️ Tunnel: %s", tunnel);

  // Configure cloud client
  Cloud::Config cfg;
  cfg.enabled = true;
  cfg.baseUrl = baseUrl;
  cfg.deviceId = deviceId;
  cfg.authToken = authToken;
  cfg.tunnel = tunnel;
  cfg.autoReconnect = autoReconnect;
  cfg.reconnectDelay = reconnectDelay;

  // Set message handler - forward cloud messages to control socket handler
  cloud.onMessage([this](JsonDocument &message) {
    const char *type = message["type"];

    // Handle cloud-specific messages
    if (type && strcmp(type, "ui_connected") == 0) {
      log(INFO, "☁️ UI client connected via cloud");
      return;
    }

    if (type && strcmp(type, "ui_disconnected") == 0) {
      log(INFO, "☁️ UI client disconnected from cloud");
      return;
    }

    // Forward all other messages to local control socket handler
    // This allows cloud UI to use same commands as local UI
    log(DEBUG, "☁️ Received message from cloud UI: %s", type ? type : "unknown");

    // Note: We need to bridge messages between cloud and local control socket
    // For now, just log them. Full integration requires bidirectional
    // forwarding.
  });

  // Initialize and connect
  if (!cloud.begin(cfg)) {
    log(ERROR, "☁️ Failed to initialize cloud client");
    return;
  }

  log(INFO, "☁️ Cloud client started");
  log(INFO, "☁️ Claim code: %s (share with users to pair device)",
      cloud.getClaimCode());
}
