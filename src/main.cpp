#include <ESPWiFi.h>

// Web Server
ESPWiFi wifi;

void setup() { wifi.start(); }

void loop() { wifi.handleClient(); }