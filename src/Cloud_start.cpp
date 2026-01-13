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
  const char *tunnel = config["cloud"]["tunnel"];
  bool autoReconnect = config["cloud"]["autoReconnect"];
  uint32_t reconnectDelay = config["cloud"]["reconnectDelay"];

  // Use current hostname as device ID (override config if needed)
  std::string hostname = config["hostname"].as<std::string>();
  if (hostname.empty()) {
    hostname = genHostname();
  }
  const char *deviceId = hostname.c_str();

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

    // Forward all other messages (device control commands) to the processing
    // logic This allows cloud UI to use same commands as local UI
    const char *cmd = message["cmd"];
    log(DEBUG, "☁️ Processing cloud message: cmd=%s", cmd ? cmd : "(none)");

    // Process the command using the same logic as local WebSocket
    JsonDocument response;
    handleCloudControlMessage(message, response);

    // Send response back through cloud tunnel
    cloud.sendMessage(response);
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
