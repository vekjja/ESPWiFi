#include "ESPWiFi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.start();
  espwifi.toggleWiFi();
  espwifi.runSystem();
}
