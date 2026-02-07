#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initFilesystem();
  startLogging();
  readConfig();
  initTFT();
}

void ESPWiFi::runSystem() {
  // Feed early so long-running work cannot starve the watchdog
  feedWatchDog();
  flushDeferredLog();
  feedWatchDog(1);
  handleConfigUpdate();
  feedWatchDog(1);
  checkSDCard();
  feedWatchDog(1);
  streamCamera();
  feedWatchDog(1);
  renderTFT();    // can run click handlers and LVGL draw (long)
  feedWatchDog(); // Feed after render so next iteration is covered
}

#endif // ESPWiFi_DEVICE