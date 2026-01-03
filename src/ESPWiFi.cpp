#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initFilesystem();
  startLogging();
  readConfig();
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
#ifdef ESPWiFi_CAMERA_ENABLED
  streamCamera();
#endif
  feedWatchDog();
  // static unsigned long lastHeartbeat = 0;
  // runAtInterval(18000, lastHeartbeat, [this]() { log(DEBUG, "🫀"); });
}

#endif // ESPWiFi_DEVICE