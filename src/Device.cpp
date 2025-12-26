#ifndef ESPWiFi_DEVICE
#define ESPWiFi_DEVICE

#include "ESPWiFi.h"

void ESPWiFi::start() {
  printf("\n\n=== ESPWiFi Starting ===\n");

  startSerial();
  printf("Serial started\n");

  startLogging();
  printf("Logging started\n");

  readConfig();
  printf("Config read\n");

  printf("=== ESPWiFi Started ===\n\n");

  // setMaxPower();
  // startWiFi();
  // startMDNS();
  // startWebServer();
  // startBluetooth();
  // startRSSIWebSocket();
  // handleConfig();
}

void ESPWiFi::runSystem() {
  // Just delay for now - reduce logging spam
  vTaskDelay(pdMS_TO_TICKS(1000));
  log(INFO, "ESPWiFi Running System");
  // yield();
  // streamRSSI();
  // checkBluetoothConnectionStatus();
  // #ifdef ESPWiFi_CAMERA
  //   streamCamera();
  // #endif
}

#endif // ESPWiFi_DEVICE