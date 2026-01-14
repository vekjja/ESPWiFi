#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  config = defaultConfig();
  initFilesystem();
  startLogging();
  readConfig();
  initNVS();
  startBLE();
  startWiFi();
  startMDNS();
  startWebServer();
  startControlWebSocket();
  startCameraWebSocket();
  // startCloudCtl();
  // startCloudMedia();
  srvAll();
}

void ESPWiFi::runSystem() {
  flushDeferredLog();   // Process deferred ESP-IDF log messages
  handleConfigUpdate(); // Handle config updates from dashboard
  checkSDCard();        // Check if SD card is present and mount it if it is
  streamCamera();       // Stream camera frames to WebSocket clients
  feedWatchDog();       // Feed the watchdog to prevent it from timing out
}

#endif // ESPWiFi_DEVICE