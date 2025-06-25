#include <ESPWiFi.h>

// ESP WiFi Capable Device:
//     ESP8266 or ESP32 device with WiFi
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
  device.streamCamera();
#ifdef ESP8266
  device.mDSNUpdate();
#endif
}