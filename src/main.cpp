#include <ESPWiFi.h>
#include <WebSocket.h>

ESPWiFi device;

void setup() {
  device.startLog();
  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
  device.srvAll();
}

void loop() {
  yield();

  device.streamRSSI();
  // device.streamMicrophone();
#ifdef ESPWiFi_CAMERA_ENABLED
  device.streamCamera();
#endif

#ifdef ESP8266
  device.updateMDNS();
#endif
}