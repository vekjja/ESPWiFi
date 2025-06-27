#include <ESPWiFi.h>

// ESP WiFi Capable Device:
//     ESP8266 or ESP32 device with WiFi
ESPWiFi device;

void setup() {
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
#ifdef ESP32
  device.startCamera();
#endif
#ifdef ESP8266
  device.startSSIDSpoof();
#endif
  device.startWebServer();
}

IntervalTimer s10Timer(10000);

void loop() {
#ifdef ESP32
  device.streamCamera();
#endif
#ifdef ESP8266
  device.handleSSIDSpoof();
  device.mDSNUpdate();
#endif
}