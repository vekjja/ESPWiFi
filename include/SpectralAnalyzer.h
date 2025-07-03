#ifndef SPECTRAL_ANALYZER_H
#define SPECTRAL_ANALYZER_H

#include <arduinoFFT.h>

// Audio Device Configuration
#define AUDIO_PIN 4
const int maxInput = 4095;
const uint16_t audioSamples = 256; // This value MUST be a power of 2
const int usableSamples = (audioSamples / 2);
double vReal[audioSamples];
double vImaginary[audioSamples];
int *spectralData = nullptr; // Array to store spectral data for matrix
const double samplingFrequency = 16000.0;

ArduinoFFT<double> FFT =
    ArduinoFFT<double>(vReal, vImaginary, audioSamples, samplingFrequency);

// Configurable Values
const int minSensitivity = 1;
const int maxSensitivity = 100;
int sensitivity = 9;

double *smoothedSpectralData = nullptr; // For smoothing

void ESPWiFi::startSpectralAnalyzer() {
  pinMode(AUDIO_PIN, INPUT); // Set audio pin as input
  log("📊 Spectral Analyzer started");
}

void spectralAnalyzer(int matrixWidth, int matrixHeight) {
  if (spectralData == nullptr) {
    spectralData = new int[matrixWidth]; // Allocate for matrix width
  }

  // Read audio samples
  for (int i = 0; i < audioSamples; i++) {
    vReal[i] = analogRead(AUDIO_PIN) * (sensitivity / 10.0); // Read ADC value
    vImaginary[i] = 0; // Initialize imaginary part
  }

  // Perform FFT
  FFT.windowing(FFTWindow::Blackman_Harris,
                FFTDirection::Forward); // Apply window
  FFT.compute(FFTDirection::Forward);   // Compute FFT
  FFT.complexToMagnitude();             // Get magnitude spectrum

  // Logarithmic bin mapping, skip first 4 bins, fill all columns
  int minBin = 4; // Skip DC and low bins
  int maxBin = usableSamples;
  double logMin = log(minBin);
  double logMax = log(maxBin);
  for (int x = 0; x < matrixWidth; x++) {
    int startBin = (int)exp(logMin + (logMax - logMin) * x / matrixWidth);
    int endBin = (int)exp(logMin + (logMax - logMin) * (x + 1) / matrixWidth);
    if (startBin < minBin)
      startBin = minBin;
    if (endBin > maxBin)
      endBin = maxBin;
    if (endBin <= startBin)
      endBin = startBin + 1; // Ensure at least one bin per column
    if (endBin > maxBin)
      endBin = maxBin;
    double peak = 0;
    for (int bin = startBin; bin < endBin && bin < usableSamples; bin++) {
      peak = max(peak, vReal[bin]);
    }
    int mapped = map(peak, 0, maxInput, 0, matrixHeight);
    mapped = constrain(mapped, 0, matrixHeight);
    spectralData[x] = mapped;
  }
}

#endif // SPECTRAL_ANALYZER_H