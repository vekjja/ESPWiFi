#include <ESPWiFi.h>

// Web Server
ESPWiFi wifi;

void setup() {
  wifi.start();
  wifi.initSSIDSpoof();
  wifi.initGPIO();
}

void loop() { wifi.handleClient(); }