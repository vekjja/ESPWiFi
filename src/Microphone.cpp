#ifndef ESPWiFi_MICROPHONE
#define ESPWiFi_MICROPHONE

#include "ESPWiFi.h"
#include "driver/i2s.h"
#include "esp_err.h"

// Microphone state variables
bool microphoneInitialized = false;
unsigned long lastMicrophoneStream = 0;
int microphoneStreamInterval = 100; // ms between audio samples

// Audio buffer for streaming
#define AUDIO_BUFFER_SIZE 1024
uint8_t audioBuffer[AUDIO_BUFFER_SIZE];

void ESPWiFi::startMicrophone() {
  if (!config["microphone"]["enabled"].as<bool>()) {
    log("🎤 Microphone Disabled");
    return;
  }

  // Ensure we have valid configuration values with defaults
  int sampleRate = config["microphone"]["sampleRate"].as<int>();
  if (sampleRate <= 0) {
    sampleRate = 16000; // Default to 16kHz
    config["microphone"]["sampleRate"] = sampleRate;
  }

  float gain = config["microphone"]["gain"].as<float>();
  if (gain <= 0) {
    gain = 1.0; // Default to 1.0
    config["microphone"]["gain"] = gain;
  }

  // I2S configuration for PDM mode on XIAO ESP32S3 Sense
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
      .sample_rate = (uint32_t)sampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format =
          I2S_COMM_FORMAT_STAND_I2S, // Use non-deprecated format
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 64,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  // XIAO ESP32S3 Sense PDM microphone pins (fixed)
  i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_PIN_NO_CHANGE,
      .ws_io_num = 42, // PDM clock pin
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = 41 // PDM data pin
  };

  esp_err_t result = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (result != ESP_OK) {
    logError("Failed to install I2S driver for PDM: " +
             String(esp_err_to_name(result)));
    return;
  }

  result = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (result != ESP_OK) {
    logError("Failed to set I2S pins for PDM: " +
             String(esp_err_to_name(result)));
    i2s_driver_uninstall(I2S_NUM_0);
    return;
  }

  log("✅ XIAO ESP32S3 Sense PDM microphone initialized successfully");
  log("   Data Pin: 41, Clock Pin: 42");

  microphoneInitialized = true;
  log("🎤 Microphone Started");
  logf("   Sample Rate: %d Hz\n", sampleRate);
  logf("   Gain: %.1f\n", gain);
}

void ESPWiFi::streamMicrophone() {
  if (!microphoneInitialized || !config["microphone"]["enabled"].as<bool>()) {
    return;
  }

  unsigned long currentTime = millis();
  if (currentTime - lastMicrophoneStream < microphoneStreamInterval) {
    return;
  }
  lastMicrophoneStream = currentTime;

  size_t bytesRead = 0;
  esp_err_t result =
      i2s_read(I2S_NUM_0, audioBuffer, AUDIO_BUFFER_SIZE, &bytesRead, 0);

  if (result == ESP_OK && bytesRead > 0) {
    // Apply gain if configured
    float gain = config["microphone"]["gain"].as<float>();
    if (gain <= 0)
      gain = 1.0; // Safety check
    if (gain != 1.0) {
      for (size_t i = 0; i < bytesRead; i += 2) {
        int16_t sample = (audioBuffer[i + 1] << 8) | audioBuffer[i];
        sample = (int16_t)(sample * gain);
        audioBuffer[i] = sample & 0xFF;
        audioBuffer[i + 1] = (sample >> 8) & 0xFF;
      }
    }

    // Simple noise reduction (basic high-pass filter)
    if (config["microphone"]["noiseReduction"].as<bool>()) {
      static int16_t prevSample = 0;
      for (size_t i = 0; i < bytesRead; i += 2) {
        int16_t currentSample = (audioBuffer[i + 1] << 8) | audioBuffer[i];
        int16_t filteredSample = currentSample - prevSample + (prevSample >> 3);
        prevSample = currentSample;
        audioBuffer[i] = filteredSample & 0xFF;
        audioBuffer[i + 1] = (filteredSample >> 8) & 0xFF;
      }
    }

    // Auto gain control
    if (config["microphone"]["autoGain"].as<bool>()) {
      static float autoGain = 1.0;
      static unsigned long lastGainUpdate = 0;

      if (currentTime - lastGainUpdate > 1000) { // Update every second
        int16_t maxSample = 0;
        for (size_t i = 0; i < bytesRead; i += 2) {
          int16_t sample = abs((audioBuffer[i + 1] << 8) | audioBuffer[i]);
          if (sample > maxSample)
            maxSample = sample;
        }

        if (maxSample > 0) {
          float targetGain = 30000.0 / maxSample; // Target 75% of max range
          autoGain = (autoGain * 0.9) + (targetGain * 0.1); // Smooth adjustment
          autoGain = constrain(autoGain, 0.1, 10.0);
        }
        lastGainUpdate = currentTime;
      }

      // Apply auto gain
      for (size_t i = 0; i < bytesRead; i += 2) {
        int16_t sample = (audioBuffer[i + 1] << 8) | audioBuffer[i];
        sample = (int16_t)(sample * autoGain);
        audioBuffer[i] = sample & 0xFF;
        audioBuffer[i + 1] = (sample >> 8) & 0xFF;
      }
    }

    // Calculate RMS for audio level monitoring
    int32_t rmsSum = 0;
    int sampleCount = 0;
    if (bytesRead >= 2) { // Ensure we have at least one sample
      for (size_t i = 0; i < bytesRead; i += 2) {
        int16_t sample = (audioBuffer[i + 1] << 8) | audioBuffer[i];
        rmsSum += sample * sample;
        sampleCount++;
      }
    }

    int16_t rms = 0;
    if (sampleCount > 0) {
      rms = sqrt(rmsSum / sampleCount);
    }

    // Debug: Log audio data with better analysis
    static int logCounter = 0;
    if (logCounter++ % 10 == 0) { // Log every 10th reading
      // Calculate statistics using the same samples as RMS
      int16_t minVal = 32767, maxVal = -32768;
      int32_t sum = 0;

      for (size_t i = 0; i < bytesRead; i += 2) {
        int16_t sample = (int16_t)(audioBuffer[i + 1] << 8 | audioBuffer[i]);
        if (sample < minVal)
          minVal = sample;
        if (sample > maxVal)
          maxVal = sample;
        sum += abs(sample);
      }

      int avgLevel = sampleCount > 0 ? sum / sampleCount : 0;

      logf("🎤 PDM Audio - RMS: %d, Avg: %d, Min: %d, Max: %d, Range: %d, "
           "Samples: %d\n",
           rms, avgLevel, minVal, maxVal, maxVal - minVal, sampleCount);
    }
  } else if (result != ESP_OK) {
    static int errorCounter = 0;
    if (errorCounter++ % 100 == 0) {
      logError("I2S read error: " + String(esp_err_to_name(result)));
    }
  }
}

#endif // ESPWiFi_MICROPHONE