#include <ESPWiFi.h>
#include <WebSocket.h>

ESPWiFi device;

void setup() { device.start(); }
void loop() { device.runSystem(); }