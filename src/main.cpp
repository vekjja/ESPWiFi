#include <ESPWiFi.h>
#include <WebSocket.h>

ESPWiFi device;
WebSocket *rssiSoc;

void setup() {
  device.startSerial();
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
#ifdef ESPWiFi_CAMERA_ENABLED
  device.startCamera();
#endif
  rssiSoc = new WebSocket("/rssi", &device);
  device.startWebServer();
}

void loop() {
  if (device.s1Timer.shouldRun()) {
    if (rssiSoc) {
      int rssi = WiFi.RSSI() | 0;
      rssiSoc->textAll(String(rssi));
    }
  }
#ifdef ESPWiFi_CAMERA_ENABLED
  device.streamCamera();
#endif
#ifdef ESP8266
  device.mDSNUpdate();
#endif
}