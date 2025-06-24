#include <ESPWiFi.h>

// Web Server
ESPWiFi device;

void setup() {
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
  device.startWebSocket();
  device.startWebServer();
}

IntervalTimer s1Timer(1000);
IntervalTimer s10Timer(10000);

void loop() {
  if (s1Timer.shouldRun()) {
    device.sendWebSocketMessage("Hello from ESP32-CAM!");
  }
  if (s10Timer.shouldRun()) {
    device.sendWebSocketMessage("Heart Beat: ❤️");
  }
}