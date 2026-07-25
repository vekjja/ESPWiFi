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

    std::string response =
        espwifi.oai_completion("What is your NATO alphabet call sign?");

    espwifi.log(INFO, "🤖 OpenAI response: %s", response.c_str());
    espwifi.oai_TTS(response, ESPWiFi::ESPWiFi_DAC_PIN_1, 0.9f);
    while (espwifi.audioPlaying) {
      espwifi.feedWatchDog();
    }

    espwifi.playAudio("/wsce496.wav", 0.9f, ESPWiFi::ESPWiFi_DAC_PIN_1);
    while (espwifi.audioPlaying) {
      espwifi.feedWatchDog();
    }

    espwifi.feedWatchDog(9000);
  }
}
