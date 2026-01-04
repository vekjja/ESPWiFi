#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initFilesystem();
  startLogging();
  readConfig();

#ifdef ESPWiFi_CAMERA_INSTALLED
  // If this firmware is built with camera support, always advertise that fact
  // in /api/config so the dashboard can render camera controls. Older saved
  // configs may have `camera.installed=false` from builds without camera.
  if (config["camera"]["installed"].isNull() ||
      !config["camera"]["installed"].as<bool>()) {
    config["camera"]["installed"] = true;
    requestConfigSave();
  }

  // Boot-time camera bring-up: if the user has enabled camera streaming,
  // initialize the driver right away so it's available immediately after boot.
  if (config["camera"]["installed"].as<bool>() &&
      config["camera"]["enabled"].as<bool>()) {
    if (!initCamera()) {
      log(ERROR,
          "📷 Camera enabled in config but failed to initialize at boot");
    }
  }
#endif

  startWiFi();
  startMDNS();
  startWebServer();
  startRSSIWebSocket();
  srvAll();
}

void ESPWiFi::runSystem() {
  handleConfigUpdate();
  checkSDCard();
  streamRSSI();
#ifdef ESPWiFi_CAMERA_INSTALLED
  streamCamera();
#endif
  feedWatchDog();
}

#endif // ESPWiFi_DEVICE