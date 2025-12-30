#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startSerial();
  startLogging();
  initLittleFS();
  readConfig();
  // setMaxPower();
  startWiFi();
  // startMDNS();
  startWebServer();
  startRSSIWebSocket();
  handleConfig();
  srvAll();
}

void ESPWiFi::runSystem() {
  yield();

  // Apply + save config changes in the main task
  // (keeps HTTP handlers fast/safe)
  if (configNeedsUpdate) {
    handleConfig();
    configNeedsUpdate = false;
  }
  if (configNeedsSave) {
    saveConfig();
    configNeedsSave = false;
  }

  // static unsigned long lastHeartbeat = 0;
  // runAtInterval(18000, lastHeartbeat, [this]() { log(DEBUG, "🫀"); });

  streamRSSI();
}

#endif // ESPWiFi_DEVICE