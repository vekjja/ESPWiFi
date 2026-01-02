#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig(); // Start with default config
  startSerial();            // Start serial
  initLittleFS();           // Initialize filesystem lfs and sd card
  startLogging();           // Start logging (will use default config values)
  readConfig();             // Load config
  // setMaxPower();
  startWiFi();
  // startMDNS();
  startWebServer();
  startRSSIWebSocket();
  srvAll();
}

void ESPWiFi::runSystem() {
  handleConfigUpdate();
  checkSDCard();
  streamRSSI();
  feedWatchDog();
  // static unsigned long lastHeartbeat = 0;
  // runAtInterval(18000, lastHeartbeat, [this]() { log(DEBUG, "🫀"); });
}

#endif // ESPWiFi_DEVICE