#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  startSerial();
  startLogging();
  readConfig();
  setMaxPower();
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
  checkBluetoothConnectionStatus();
#ifdef ESPWiFi_CAMERA
  streamCamera();
#endif
}

#endif // ESPWiFi_DEVICE