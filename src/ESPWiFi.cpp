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
  startControlWebSocket();
  srvAll();
}

void ESPWiFi::runSystem() {
  handleConfigUpdate();
  checkSDCard();
#if ESPWiFi_HAS_CAMERA
  streamCamera();
#endif
  feedWatchDog();
}

#endif // ESPWiFi_DEVICE