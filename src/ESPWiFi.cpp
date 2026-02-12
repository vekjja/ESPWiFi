#ifndef ESPWiFi_RUNTIME
#define ESPWiFi_RUNTIME

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initFilesystem();
  startLogging();
  readConfig();
  initTFT();
#ifdef CONFIG_BT_A2DP_ENABLE
  startBluetooth();
#endif
}

void ESPWiFi::runSystem() {
  handleConfigUpdate();
  checkSDCard();
  streamCamera();
  renderTFT();     // can run click handlers and LVGL draw (long)
  feedWatchDog();  // Feed after render so next iteration is covered
}

#endif  // ESPWiFi_RUNTIME