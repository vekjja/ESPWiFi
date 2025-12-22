#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startSerial();
  startLogging();
  setMaxPower();
  readConfig();
  startWiFi();
  startMDNS();
  startWebServer();
  startBluetooth();
  startRSSIWebSocket();
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

#endif // ESPWiFi_DEVICE