#include "ESPWiFi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.start();
  // espwifi.toggleWiFi();

  for (;;) {
    espwifi.feedWatchDog();
    espwifi.handleConfigUpdate();

    espwifi.playAudio("/wsce496.wav", 0.9f, ESPWiFi::ESPWiFi_DAC_PIN_1);
    espwifi.setGPIO(27, 1);
    while (espwifi.audioPlaying) {
      espwifi.feedWatchDog();
    }
    espwifi.setGPIO(27, 0);
    espwifi.log(INFO, "Done playing audio");
    espwifi.feedWatchDog(6000);
  }
}
