#include "ESPWiFi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.audioPttPin = 27;

  espwifi.start();
  espwifi.toggleWiFi();

  for (;;) {
    espwifi.feedWatchDog();
    espwifi.handleConfigUpdate();

    std::string response = espwifi.oai_completion("Simple Polite Radio Hello");

    espwifi.log(INFO, "🤖 OpenAI response: %s", response.c_str());
    espwifi.oai_TTS(response, ESPWiFi::ESPWiFi_DAC_PIN_1, 1.0f);
    while (espwifi.audioPlaying) {
      espwifi.feedWatchDog();
    }

    espwifi.feedWatchDog(30000);
  }
}
