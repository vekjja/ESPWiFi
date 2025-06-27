#include <ESPWiFi.h>
#include <WebSocket.h>

ESPWiFi device;
WebSocket wsServer;

void setup() {
  device.startSerial();
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
#ifdef ESP32
  device.startCamera();
#endif
  wsServer = WebSocket("/rssi", &device);
  device.startWebServer();
}

void loop() {
  if (device.s1Timer.shouldRun()) {
    if (wsServer) {
      int rssi = WiFi.RSSI() | 0;
      wsServer.textAll(String(rssi));
    }
  }
#ifdef ESP32
  device.streamCamera();
#endif
#ifdef ESP8266
  device.mDSNUpdate();
#endif
}