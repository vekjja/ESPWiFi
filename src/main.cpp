#include <cmath>

#include "ESPWiFi.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.start();
  espwifi.toggleWiFi();

  espwifi.audioPttPin = 12;

  const int rxPin = 34;
  static constexpr const char* kRxWav = "rx.wav";
  static constexpr float kRxIdleVolts = 1.65f;
  static constexpr float kRxActiveDelta = 0.18f;
  static constexpr float kRxIdleDelta = 0.08f;
  static constexpr int kRxActiveHoldMs = 250;
  static constexpr int kRxIdleHoldMs = 400;

  bool rxSession = false;
  unsigned long activeSinceMs = 0;
  unsigned long idleSinceMs = 0;

  for (;;) {
    espwifi.handleConfigUpdate();

    if (espwifi.isRxRecording()) {
      espwifi.feedWatchDog();
      continue;
    }

    const float rxLevel = espwifi.readAnalog(rxPin);
    const float rxDeviation = std::fabs(rxLevel - kRxIdleVolts);
    const bool signalActive = rxDeviation > kRxActiveDelta;
    const bool signalIdle = rxDeviation < kRxIdleDelta;
    const unsigned long now = espwifi.millis();

    if (signalActive) {
      idleSinceMs = 0;
      if (activeSinceMs == 0) {
        activeSinceMs = now;
      }
    } else if (signalIdle) {
      activeSinceMs = 0;
      if (idleSinceMs == 0) {
        idleSinceMs = now;
      }
    }

    const bool rxActive =
        signalActive && activeSinceMs != 0 &&
        (now - activeSinceMs) >= static_cast<unsigned long>(kRxActiveHoldMs);

    const bool rxStopped =
        signalIdle && idleSinceMs != 0 &&
        (now - idleSinceMs) >= static_cast<unsigned long>(kRxIdleHoldMs);

    if (!rxSession && rxActive) {
      rxSession = true;
      espwifi.log(INFO, "📡 RX detected: %.3f V (%.0f mV from idle)", rxLevel,
                  rxDeviation * 1000.0f);
      espwifi.startRxRecording(rxPin, kRxWav);
      espwifi.feedWatchDog();
      continue;
    }

    if (rxSession && rxStopped) {
      espwifi.log(INFO, "📡 RX stopped (idle %d ms)", kRxIdleHoldMs);
      // espwifi.playAudio("chime.wav", 1.0f);
      // while (espwifi.audioPlaying) {
      //   espwifi.feedWatchDog();
      // }

      if (espwifi.hasRxRecording()) {
        espwifi.log(INFO, "📢 Responding to RX (captured %s)", kRxWav);
        espwifi.playAudio(kRxWav, 1.0f);
        while (espwifi.audioPlaying) {
          espwifi.feedWatchDog();
        }
      } else {
        espwifi.log(WARNING, "📡 RX ended with no captured audio");
      }

      espwifi.clearRxRecording();
      rxSession = false;
      activeSinceMs = 0;
      idleSinceMs = 0;
    }

    espwifi.feedWatchDog();
  }
}
