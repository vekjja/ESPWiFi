#include <ESPWiFi.h>

// Web Server
ESPWiFi device;

void setup() {
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
  device.startWebServer();
}

void loop() {}