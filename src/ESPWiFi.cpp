#ifndef ESPWiFi_RUNTIME
#define ESPWiFi_RUNTIME

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initFilesystem();
  startLogging();
  readConfig();
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
    // streamCamera();
    renderTFT();     // Run click handlers and LVGL draw
    feedWatchDog();  // Feed after render to keep watchdog happy
  }
}

#endif  // ESPWiFi_RUNTIME