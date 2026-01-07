#include "ESPWiFi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.start();
  while (1) {
    espwifi.runSystem();
    espwifi.feedWatchDog();
  }
}