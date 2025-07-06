#include <ESPWiFi.h>
#include <WebSocket.h>

#ifdef ESPWiFi_TOUCH_DISPLAY_ENABLED
#include <TouchDisplay.h>
#endif

ESPWiFi device;

#ifdef ESPWiFi_TOUCH_DISPLAY_ENABLED
// Touch display callback functions
void onSettingsButton(uint16_t id, TouchEvent event) {
  display.drawSettingsScreen();
}

void onStatusButton(uint16_t id, TouchEvent event) {
  display.drawStatusScreen();
}

void onRestartButton(uint16_t id, TouchEvent event) {
  display.clear();
  display.drawTextCentered(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2,
                           "Restarting...", TEXT_COLOR, 3);
  delay(1000);
  ESP.restart();
}

void onBackToMainButton(uint16_t id, TouchEvent event) {
  display.drawMainScreen();
}

void onBrightnessSlider(uint16_t id, TouchEvent event) {
  // Calculate brightness based on touch position
  uint8_t brightness = map(event.x, 20, 220, 0, 255);
  display.setBrightness(brightness);
}
#endif

void setup() {
  device.startLog();

#ifdef ESPWiFi_TOUCH_DISPLAY_ENABLED
  // Initialize touch display
  if (display.begin()) {
    device.log("Touch display initialized successfully");
  } else {
    device.log("Failed to initialize touch display");
  }
#endif

  device.startWiFi();
  device.startMDNS();
  device.startGPIO();
#ifdef ESPWiFi_CAMERA_ENABLED
  device.startCamera();
#endif
  device.srvAll();
  device.startWebServer();

#ifdef ESPWiFi_TOUCH_DISPLAY_ENABLED
  // Set up touch display callbacks
  display.drawMainScreen();
#endif
}

void loop() {
  yield();

#ifdef ESPWiFi_TOUCH_DISPLAY_ENABLED
  // Update touch display
  display.update();
#endif

  device.streamRSSI();

#ifdef ESPWiFi_CAMERA_ENABLED
  device.streamCamera();
#endif

#ifdef ESP8266
  device.updateMDNS();
#endif
}