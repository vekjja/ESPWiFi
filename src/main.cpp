#include <ESPWiFi.h>

// Web Server
ESPWiFi wifi;

void setup() {
  wifi.start();
  wifi.startGPIO();  // Initialize GPIO pins
}

void loop() { wifi.handleClient(); }