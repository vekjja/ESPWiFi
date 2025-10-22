#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startLogging();
  startWiFi();
  startMDNS();
  startWebServer();
  handleConfig();
}

void ESPWiFi::runSystem() {
  yield();
  streamRSSI();

#ifdef ESPWiFi_CAMERA_INSTALLED
  if (config["camera"]["enabled"]) {
    streamCamera();
    updateRecording();
  }
#endif

#ifdef ESP8266
  updateMDNS();
#endif
}

#endif