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
  device.stopSleep();
  device.startWebServer();
}

void loop() {
  // Feed the watchdog timer to prevent resets
  yield();

  device.streamRssi();

#ifdef ESPWiFi_CAMERA_ENABLED
  device.streamCamera();
#endif
#ifdef ESP8266
  device.mDSNUpdate();
#endif
}