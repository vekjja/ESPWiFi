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
  device.stopSleep();
  device.startWebServer();
}

void loop() {
  // Feed the watchdog timer to prevent resets
  yield();

  if (device.s1Timer.shouldRun()) {
    if (rssiSoc) {
      // Use static buffer to prevent memory fragmentation
      static char rssiBuffer[16];
      int rssi = WiFi.RSSI() | 0;
      snprintf(rssiBuffer, sizeof(rssiBuffer), "%d", rssi);
      rssiSoc->textAll(String(rssiBuffer));
    }
  }

#ifdef ESPWiFi_CAMERA_ENABLED
  device.streamCamera();
#endif
#ifdef ESP8266
  device.mDSNUpdate();
#endif
}