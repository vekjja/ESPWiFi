#include <ESPWiFi.h>
#include <IntervalTimer.h>

// Web Server
ESPWiFi device;

void setup() {
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
  device.startCamera();
  device.startWebServer();
}

IntervalTimer s10Timer(10000);

void loop() {
  if (s10Timer.shouldRun()) {
#ifdef ESP8266
    device.mDSNUpdate();
#endif
  }
}