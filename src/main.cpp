#include <ESPWiFi.h>
#include <WebSocket.h>

ESPWiFi device;

void setup() {
  device.startLog();
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
#ifdef ESPWiFi_CAMERA_ENABLED
  device.startCamera();
#endif
  device.srvAll();
  device.startWebServer();
}

void loop() {
  yield();

  device.streamRSSI();

#ifdef ESPWiFi_CAMERA_ENABLED
  device.streamCamera();
#endif

#if CONFIG_IDF_TARGET_ESP8266
  device.updateMDNS();
#endif
}