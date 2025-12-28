#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startSerial();
  startLogging();
  readConfig();
  // setMaxPower();
  startWiFi();
  // startMDNS();
  startWebServer();
  srvRoot();
  srvConfig();
  srvWildcard();
  // startBluetooth();
  // startRSSIWebSocket();
  // handleConfig();
}

void ESPWiFi::runSystem() {
  taskYIELD();

  // Check if config needs saving (deferred from HTTP handlers)
  if (configNeedsSave) {
    saveConfig(); // Save from main task context (safe for filesystem)
    configNeedsSave = false;
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