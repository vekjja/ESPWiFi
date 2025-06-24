#include <ESPWiFi.h>

// Web Server
ESPWiFi device;

void setup() {
  device.startWiFi();
  device.startGPIO();
}

void loop() { device.handleClient(); }