#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startSerial();
  startLogging();
  readConfig();
  startWiFi();
  startMDNS();
  startWebServer();
  handleConfig();
}

void ESPWiFi::runSystem() {
  yield();
  streamRSSI();

#ifdef ESPWiFi_CAMERA
  if (config["camera"]["enabled"]) {
    streamCamera();
  }
#endif
}

#endif