#ifndef ESPWiFi_RUNTIME
#define ESPWiFi_RUNTIME

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initFilesystem();
  startLogging();
  readConfig();
  initTFT();
  startBluetooth();
}

void ESPWiFi::runSystem() {
  // Feed early so long-running work cannot starve the watchdog
  flushDeferredLog();
  handleConfigUpdate();
  checkSDCard();
  streamCamera();
  renderTFT();    // can run click handlers and LVGL draw (long)
  feedWatchDog(); // Feed after render so next iteration is covered
}

#endif // ESPWiFi_RUNTIME