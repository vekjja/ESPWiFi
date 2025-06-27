#include <ESPWiFi.h>

// ESP WiFi Capable Device:
//     ESP8266 or ESP32 device with WiFi
ESPWiFi device;

void setup() {
  device.startSerial();
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
#ifdef ESP32
  device.startCamera();
#endif
  device.startWebServer();
}

void loop() {
#ifdef ESP32
  device.streamCamera();
#endif
#ifdef ESP8266
  device.mDSNUpdate();
#endif
}