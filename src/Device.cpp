#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startLogging();
  startWiFi();
  startMDNS();
  startWebServer();
}

void ESPWiFi::runSystem() {
  yield();
  streamRSSI();

#ifdef ESPWiFi_CAMERA_ENABLED
  streamCamera();
#endif

#ifdef ESP8266
  updateMDNS();
#endif
}

#endif