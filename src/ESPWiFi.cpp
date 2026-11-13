#ifndef ESPWiFi_RUNTIME
#define ESPWiFi_RUNTIME

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initLittleFS();
  readConfig();
  initSDCard();
  startLogging();
#if ESPWiFi_HAS_TFT
  initTFT();
#endif
#ifdef CONFIG_BT_A2DP_ENABLE
  startBluetooth();
#endif
}

void ESPWiFi::runSystem() {
  for (;;) {
    feedWatchDog();
    handleConfigUpdate();
    checkSDCard();
    streamCamera();
    renderTFT();
    feedWatchDog();
  }
}

#endif  // ESPWiFi_RUNTIME