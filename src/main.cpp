#include <ESPWiFi.h>

// Web Server
ESPWiFi wifi;

void setup() {
  wifi.start();
  wifi.initGPIO();
}

void loop() { wifi.handleClient(); }