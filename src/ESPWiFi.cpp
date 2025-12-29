#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startSerial();
  // Load config before starting logging so we can select the log filesystem
  // (prefer SD when available) and mount SD only when enabled in config.
  initLittleFS();
  readConfig();
  startLogging();
  // setMaxPower();
  startWiFi();
  // startMDNS();
  startWebServer();
  srvAll();
  // startBluetooth();
  // startRSSIWebSocket();
  handleConfig();
}

void ESPWiFi::runSystem() {
  taskYIELD();

  // Apply + save config changes in the main task
  // (keeps HTTP handlers fast/safe)
  if (configNeedsUpdate) {
    handleConfig();
    saveConfig();
    configNeedsUpdate = false;
  }

  // static unsigned long lastHeartbeat = 0;
  // runAtInterval(18000, lastHeartbeat, [this]() { log(DEBUG, "🫀"); });

  // streamRSSI();
  // checkBluetoothConnectionStatus();
  // #ifdef ESPWiFi_CAMERA
  //   streamCamera();
  // #endif
}

#endif // ESPWiFi_DEVICE