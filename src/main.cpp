#include "ESPWiFi.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.start();
  espwifi.toggleWiFi();

  espwifi.audioPttPin = 27;
  const int rxPin = 34;
  bool receivedRX = false;

  for (;;) {
    espwifi.handleConfigUpdate();

    const float rxLevel = espwifi.readAnalog(rxPin);

    if (rxLevel > 2.15f && !receivedRX) {
      receivedRX = true;
      espwifi.log(INFO, "📡 Received RX level: %.3f V", rxLevel);
    }

    if (rxLevel < 1.0f && receivedRX) {
      receivedRX = false;
      espwifi.log(INFO, "📢 Responding to RX");
      std::string response = espwifi.oai_completion(
          "Simple and polite Ham Radio Greeting, with a HAM Radio prep fact");

      espwifi.log(INFO, "🤖 OpenAI response: %s", response.c_str());
      espwifi.oai_TTS(response, 0.9f);
      while (espwifi.audioPlaying) {
        espwifi.feedWatchDog();
      }
    }
    espwifi.feedWatchDog();
  }
}
