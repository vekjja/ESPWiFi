#include "ESPWiFi.h"
// WebSocket not yet ported to ESP-IDF
// #include "WebSocket.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// static const char *TAG = "main";

ESPWiFi espwifi;

extern "C" void app_main(void) {
  // Start device
  espwifi.start();
  // Main loop task
  while (1) {
    espwifi.runSystem();
  }
}