#include <ESPWiFi.h>
#include <IntervalTimer.h>

#include "WebSocket.h"

// Web Server
ESPWiFi device;
WebSocket* ws;

void setup() {
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
  ws = new WebSocket("/ws", &device);
  device.startWebServer();
}

IntervalTimer s10Timer(10000);

void loop() {
  if (s10Timer.shouldRun()) {
    String message = "Heart Beat: ❤️";
    ws->textAll(message);
    Serial.printf("🟢 [%s] textAll: %s\n", ws->socket->url(), message.c_str());
#ifdef ESP8266
    device.mDSNUpdate();
#endif
  }
}