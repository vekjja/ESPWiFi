
#ifndef ESPWiFi_BLUETOOTH_WAV_H
#define ESPWiFi_BLUETOOTH_WAV_H

#include "ESPWiFi.h"
#if defined(CONFIG_BT_A2DP_ENABLE)

#include <cstring>

#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/Concurrency/RTOS/BufferRTOS.h"
#include "BluetoothA2DPSource.h"

using namespace audio_tools;

// Defined in Bluetooth.cpp
extern BluetoothA2DPSource s_a2dp_source;
extern int32_t silentDataCb(uint8_t* data, int32_t len);

// ---------------------------------------------------------------------------
// WAV → A2DP streaming  (using arduino-audio-tools WAVDecoder)
//
// Architecture:
//   1.  A FreeRTOS task reads the WAV file in chunks and feeds them to
//       WAVDecoder::write().  The decoder parses the RIFF/WAVE header,
//       locates the data chunk, and converts 8/24/32-bit PCM to 16-bit.
//   2.  Decoded 16-bit PCM flows through a RingBufferPrint into a thread-safe
//       BufferRTOS (FreeRTOS StreamBuffer).
//   3.  The A2DP data callback pulls PCM from the ring buffer; silence
//       is returned when the buffer is empty to keep the link alive.
// ---------------------------------------------------------------------------

// Thread-safe ring buffer — 16 KB ≈ ~90 ms of 44100 Hz stereo 16-bit audio.
// Read/write timeouts are 0 (non-blocking) so the A2DP callback never stalls
// and the WavRingPrint does its own back-pressure loop.
static constexpr size_t kWavBufSize = 16384;
static BufferRTOS<uint8_t> wavBuf(kWavBufSize, 1, 0 /*writeWait*/,
                                  0 /*readWait*/);

// A2DP data callback for WAV playback.
static int32_t wavDataCb(uint8_t* data, int32_t len) {
  if (len <= 0) return 0;
  int got = wavBuf.readArray(data, (int)len);
  if (got < len) memset(data + got, 0, (size_t)(len - got));
  return len;
}

// Print adapter — pushes decoded PCM into the WAV ring buffer with
// back-pressure, yielding to FreeRTOS when the buffer is full.
class WavRingPrint : public Print {
 public:
  volatile bool* playing = nullptr;
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    size_t written = 0;
    while (written < len) {
      if (playing && !*playing) return written;
      int chunk = wavBuf.writeArray(data + written, (int)(len - written));
      written += (size_t)chunk;
      if (written < len) vTaskDelay(pdMS_TO_TICKS(2));
    }
    return written;
  }
};

// FreeRTOS task: reads WAV file → WAVDecoder → ring buffer → A2DP.
static void wavDecoderTaskFunc(void* param) {
  ESPWiFi* self = static_cast<ESPWiFi*>(param);
  const std::string path = self->btAudioFilePath;

  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    self->log(ERROR, "🛜🎵 Failed to open WAV file: %s", path.c_str());
    self->btAudioPlaying = false;
    self->btAudioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  // audio-tools decoder pipeline:
  //   fread() → WAVDecoder::write() → WavRingPrint → ring buf → A2DP
  WavRingPrint pcmOut;
  pcmOut.playing = &self->btAudioPlaying;

  auto* decoder = new WAVDecoder();
  if (!decoder) {
    self->log(ERROR, "🛜🎵 Failed to allocate WAV decoder");
    fclose(f);
    self->btAudioPlaying = false;
    self->btAudioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  decoder->setOutput(pcmOut);
  decoder->begin();

  s_a2dp_source.set_data_callback(wavDataCb);
  self->log(INFO, "🛜🎵 WAV playback started: %s", path.c_str());

  uint8_t readBuf[512];
  while (self->btAudioPlaying) {
    size_t n = fread(readBuf, 1, sizeof(readBuf), f);
    if (n == 0) break;
    decoder->write(readBuf, n);
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  decoder->end();
  delete decoder;
  fclose(f);

  // Drain ring buffer before restoring silence callback.
  int drainMs = 0;
  while (wavBuf.available() > 0 && drainMs < 2000) {
    vTaskDelay(pdMS_TO_TICKS(20));
    drainMs += 20;
  }

  s_a2dp_source.set_data_callback(silentDataCb);
  wavBuf.reset();

  self->log(INFO, "🛜🎵 WAV playback finished");
  self->btAudioPlaying = false;
  self->btAudioTask = nullptr;
  vTaskDelete(nullptr);
}

void ESPWiFi::startBluetoothWavPlayback(const char* path) {
  if (a2dp_source == nullptr) {
    log(WARNING, "🛜🎵 Cannot play WAV: Bluetooth not started");
    return;
  }
  if (btAudioPlaying) {
    log(INFO, "🛜🎵 Stopping current playback before starting new file");
    stopBluetoothWavPlayback();
  }

  btAudioFilePath = (path != nullptr) ? path : "";
  if (btAudioFilePath.empty()) {
    log(WARNING, "🛜🎵 No WAV file path provided");
    return;
  }
  if (btAudioFilePath[0] != '/') {
    btAudioFilePath = resolvePathOnSD(btAudioFilePath);
  }

  FILE* probe = fopen(btAudioFilePath.c_str(), "rb");
  if (!probe) {
    log(ERROR, "🛜🎵 WAV file not found: %s", btAudioFilePath.c_str());
    return;
  }
  fclose(probe);

  btAudioPlaying = true;
  wavBuf.reset();

  BaseType_t ok = xTaskCreatePinnedToCore(wavDecoderTaskFunc, "wavdec", 4096,
                                          this, 5, &btAudioTask, 1);
  if (ok != pdPASS) {
    log(ERROR, "🛜🎵 Failed to create WAV decoder task");
    btAudioPlaying = false;
    btAudioTask = nullptr;
  }
}

void ESPWiFi::stopBluetoothWavPlayback() {
  if (!btAudioPlaying) return;
  btAudioPlaying = false;
  int waitMs = 0;
  while (btAudioTask != nullptr && waitMs < 3000) {
    vTaskDelay(pdMS_TO_TICKS(50));
    waitMs += 50;
  }
  if (a2dp_source != nullptr) {
    s_a2dp_source.set_data_callback(silentDataCb);
  }
  wavBuf.reset();
  log(INFO, "🛜🎵 WAV playback stopped");
}

#endif  // CONFIG_BT_A2DP_ENABLE
#endif  // ESPWiFi_BLUETOOTH_WAV_H
