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
#ifdef ESPWiFi_CAMERA_ENABLED
  startCamera();
#endif
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
}

#endif // ESPWiFi_DEVICE