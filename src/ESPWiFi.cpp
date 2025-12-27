#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  // config = defaultConfig();

  startSerial();

  startLogging();

  readConfig();

  printf("\n\t========= ESPWiFi Started =========\n\n");

  // setMaxPower();

  startWiFi();

  // startMDNS();
  // startWebServer();
  // startBluetooth();
  // startRSSIWebSocket();
  // handleConfig();
}

void ESPWiFi::runSystem() {
  taskYIELD();

  // Just delay for now - reduce logging spam
  vTaskDelay(pdMS_TO_TICKS(1000));
  log(INFO, "🫀  ESPWiFi System Running");

  // streamRSSI();
  // checkBluetoothConnectionStatus();
  // #ifdef ESPWiFi_CAMERA
  //   streamCamera();
  // #endif
}

#endif // ESPWiFi_DEVICE