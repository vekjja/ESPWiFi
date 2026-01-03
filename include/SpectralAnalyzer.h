#ifndef ESPWiFi_SPECTRAL_ANALYZER_H
#define ESPWiFi_SPECTRAL_ANALYZER_H

#include <arduinoFFT.h>

#include "ESPWiFi.h"

// Audio Config
#define AUDIO_PIN 4
int sensitivity = 9;

const int maxInput = 4095; // ESP32-C3 has 12-bit ADC (0-4095)
int *spectralData = nullptr;

const uint16_t audioSamples = 128; // must be a power of 2
const int usableSamples = (audioSamples / 2);

double vReal[audioSamples];
double vImage[audioSamples];
const double samplingFrequency = 16000; // Hz
ArduinoFFT<double> FFT =
    ArduinoFFT<double>(vReal, vImage, audioSamples, samplingFrequency);

void ESPWiFi::startSpectralAnalyzer() {
  pinMode(AUDIO_PIN, INPUT); // Set audio pin as input
  log(INFO, "📊 Spectral Analyzer started");
  log(DEBUG, "Sampling frequency: %.2f Hz", samplingFrequency);
  log(DEBUG, "Listening on Pin: %d", AUDIO_PIN);
}

void peakDetection(int *peakData, int maxWidth, int maxHeight) {
  int binsToSkip = 4;
  int avgRange = (usableSamples - binsToSkip) /
                 maxWidth; // Reserve binsToSkip bins to skip
  // Start the loop from 1 to fill peakData[0] to peakData[maxWidth-1]
  for (int i = 1; i <= maxWidth; i++) {
    double peak = 0;
    int startFreqBin =
        (i - 1) * avgRange +
        binsToSkip; // Skip first binsToSkip bins, then map to output
    int endFreqBin = startFreqBin + avgRange;

    for (int j = startFreqBin; j < endFreqBin && j < usableSamples; j++) {
      if (vReal[j] > peak) {
        peak = vReal[j];
      }
    }
    // Map the peak value to a row on the LED matrix
    peakData[i - 1] = map(peak, 0, maxInput, 0, maxHeight);
  }
}

extern void spectralAnalyzer(int maxWidth, int maxHeight) {
  for (int i = 0; i < audioSamples; i++) {
    // Scale the audio input according to sensitivity
    vReal[i] = analogRead(AUDIO_PIN) * (sensitivity / 10.0);
    vImage[i] = 0;
  }

  // FFT.windowing(FFTWindow::Blackman_Harris, FFTDirection::Forward);
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();

  if (spectralData == nullptr) {
    spectralData = new int[maxWidth];
  }
  peakDetection(spectralData, maxWidth, maxHeight);
}

#endif // SPECTRAL_ANALYZER_H