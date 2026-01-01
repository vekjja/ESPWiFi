#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  startSerial();  // Start serial
  initLittleFS(); // Initialize filesystem lfs and sd card
  startLogging(); // Start logging (will use default config values)
  readConfig(); // Load config (logging already started, will use config on next
                // log)
  // setMaxPower();
  startWiFi();
  // startMDNS();
  startWebServer();
  startRSSIWebSocket();
  handleConfig(); // Apply config changes
  srvAll();
}

void ESPWiFi::runSystem() {
  yield();
  checkSDCardPresent();

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

  // if (sdCardCheck.shouldRun()) {
  //   if (sdCard != nullptr) {
  //     // Card is mounted - verify it's still present
  //   } else if (!sdNotSupported) {
  //     // Card was removed - try to reinitialize if it's been reinserted
  //     initSDCard();
  //     if (sdCard != nullptr) {
  //       log(INFO, "🔄 💾 SD Card Remounted: %s", sdMountPoint.c_str());
  //     }
  //   }
  // }

  // static unsigned long lastHeartbeat = 0;
  // runAtInterval(18000, lastHeartbeat, [this]() { log(DEBUG, "🫀"); });

  streamRSSI();
}

#endif // ESPWiFi_DEVICE