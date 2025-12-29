#include "ESPWiFi.h"
// WebSocket not yet ported to ESP-IDF
// #include "WebSocket.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";

ESPWiFi espwifi;

extern "C" void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "ESPWiFi Starting...");

  // Start device
  espwifi.start();

  // Main loop task
  while (1) {
    espwifi.runSystem();
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to prevent watchdog issues
  }
}