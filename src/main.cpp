#include <ESPWiFi.h>

// Web Server
ESPWiFi wifi;

void setup() {
  wifi.start();
  wifi.startGPIO();
}

void loop() { wifi.handleClient(); }