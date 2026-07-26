#include "ESPWiFi.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.start();
  espwifi.toggleWiFi();

  espwifi.audioPttPin = 12;
  espwifi.setGPIO(espwifi.audioPttPin, "low");
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
      espwifi.playAudio("chime.wav", 1.0f);

      std::string response = espwifi.oai_completion(
          "Simple and polite Ham Radio Greeting, with a HAM Radio prep fact");

      espwifi.log(INFO, "🤖 OpenAI response: %s", response.c_str());
      if (response.empty()) {
        espwifi.log(ERROR, "🤖 OpenAI: skipping TTS (empty response)");
      } else {
        espwifi.streamTTS(response, 1.0f);
      }
    }
    espwifi.feedWatchDog();
  }
}
