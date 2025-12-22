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

  // Periodically check Bluetooth connection status (callbacks may not fire)
  static unsigned long lastBtCheck = 0;
  unsigned long now = millis();
  if (now - lastBtCheck >= 1000) { // Check every second
    lastBtCheck = now;
    checkBluetoothConnectionStatus();
  }

#ifdef ESPWiFi_CAMERA
  if (config["camera"]["enabled"]) {
    streamCamera();
  }
#endif
}

#endif // ESPWiFi_DEVICE