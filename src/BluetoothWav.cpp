
#ifndef ESPWiFi_BLUETOOTH_WAV_H
#define ESPWiFi_BLUETOOTH_WAV_H

#include "ESPWiFi.h"
#if defined(CONFIG_BT_A2DP_ENABLE)

#include <cstdint>
#include <cstring>

#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/Concurrency/RTOS/BufferRTOS.h"
#include "AudioTools/CoreAudio/ResampleStream.h"
#include "BluetoothA2DPSource.h"

using namespace audio_tools;

// Defined in Bluetooth.cpp
extern BluetoothA2DPSource s_a2dp_source;
extern int32_t silentDataCb(uint8_t* data, int32_t len);

// Thread-safe ring buffer — 16 KB ≈ ~90 ms of 44100 Hz stereo 16-bit audio.
static constexpr size_t kWavBufSize = 16384;
static BufferRTOS<uint8_t> wavBuf(kWavBufSize, 1, 0 /*writeWait*/,
                                  0 /*readWait*/);
static constexpr int kA2dpTargetSampleRate = 44100;
static volatile bool wavPaused = false;

static uint16_t readLe16(const uint8_t* ptr) {
  return (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
}

static uint32_t readLe32(const uint8_t* ptr) {
  return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) |
         ((uint32_t)ptr[3] << 24);
}

static bool probeWavFormat(FILE* f, WAVAudioInfo& info) {
  if (f == nullptr) return false;

  uint8_t riff[12];
  if (fseek(f, 0, SEEK_SET) != 0) return false;
  if (fread(riff, 1, sizeof(riff), f) != sizeof(riff)) return false;
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    return false;
  }

  bool gotFmt = false;
  while (!feof(f)) {
    uint8_t chunkHeader[8];
    if (fread(chunkHeader, 1, sizeof(chunkHeader), f) != sizeof(chunkHeader)) {
      break;
    }

    uint32_t chunkSize = readLe32(chunkHeader + 4);
    bool isFmt = memcmp(chunkHeader, "fmt ", 4) == 0;
    bool isData = memcmp(chunkHeader, "data", 4) == 0;

    if (isFmt) {
      if (chunkSize < 16) return false;
      uint8_t fmt[16];
      if (fread(fmt, 1, sizeof(fmt), f) != sizeof(fmt)) return false;

      info.format = (AudioFormat)readLe16(fmt + 0);
      info.channels = (int)readLe16(fmt + 2);
      info.sample_rate = (int)readLe32(fmt + 4);
      info.byte_rate = (int)readLe32(fmt + 8);
      info.block_align = (int)readLe16(fmt + 12);
      info.bits_per_sample = (int)readLe16(fmt + 14);
      info.is_valid = true;

      long remaining = (long)chunkSize - (long)sizeof(fmt);
      if (remaining > 0 && fseek(f, remaining, SEEK_CUR) != 0) return false;
      gotFmt = true;
    } else {
      if (isData && gotFmt) {
        if (fseek(f, 0, SEEK_SET) != 0) return false;
        return true;
      }
      if (chunkSize > 0 && fseek(f, (long)chunkSize, SEEK_CUR) != 0) {
        return false;
      }
    }

    if ((chunkSize & 1U) != 0U) {
      if (fseek(f, 1, SEEK_CUR) != 0) return false;
    }
  }

  if (fseek(f, 0, SEEK_SET) != 0) return false;
  return gotFmt;
}

// A2DP data callback for WAV playback.
static int32_t wavDataCb(uint8_t* data, int32_t len) {
  if (len <= 0) return 0;
  if (wavPaused) {
    memset(data, 0, (size_t)len);
    return len;
  }
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

class MonoToStereoPrint : public Print {
 public:
  void setOutput(Print& out) { p_out = &out; }

  void setBitsPerSample(int bits) {
    if (bits <= 8) {
      bytes_per_sample = 1;
    } else if (bits <= 16) {
      bytes_per_sample = 2;
    } else {
      bytes_per_sample = 4;
    }
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t* data, size_t len) override {
    if (p_out == nullptr || data == nullptr || len == 0) return 0;

    if (bytes_per_sample <= 0 || len < (size_t)bytes_per_sample) {
      return p_out->write(data, len);
    }

    constexpr size_t kOutChunkSize = 512;
    uint8_t out_buf[kOutChunkSize];
    size_t out_pos = 0;
    size_t input_pos = 0;

    while (input_pos + (size_t)bytes_per_sample <= len) {
      const uint8_t* sample = data + input_pos;

      if (out_pos + (size_t)bytes_per_sample * 2 > kOutChunkSize) {
        p_out->write(out_buf, out_pos);
        out_pos = 0;
      }

      memcpy(out_buf + out_pos, sample, (size_t)bytes_per_sample);
      out_pos += (size_t)bytes_per_sample;
      memcpy(out_buf + out_pos, sample, (size_t)bytes_per_sample);
      out_pos += (size_t)bytes_per_sample;
      input_pos += (size_t)bytes_per_sample;
    }

    if (out_pos > 0) {
      p_out->write(out_buf, out_pos);
    }

    if (input_pos < len) {
      p_out->write(data + input_pos, len - input_pos);
    }

    return len;
  }

 protected:
  Print* p_out = nullptr;
  int bytes_per_sample = 2;
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

  WavRingPrint pcmOut;
  pcmOut.playing = &self->btAudioPlaying;

  WAVAudioInfo wavInfo;
  bool hasWavInfo = probeWavFormat(f, wavInfo);
  if (hasWavInfo) {
    self->log(INFO, "🛜🎵 WAV format: %d Hz, %d ch, %d bit",
              wavInfo.sample_rate, wavInfo.channels, wavInfo.bits_per_sample);
  } else {
    self->log(WARNING,
              "🛜🎵 Could not parse WAV format; using passthrough rate");
  }

  WAVDecoder* decoder = new WAVDecoder();
  if (!decoder) {
    self->log(ERROR, "🛜🎵 Failed to allocate WAV decoder");
    fclose(f);
    self->btAudioPlaying = false;
    self->btAudioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  MonoToStereoPrint monoExpander;
  Print* pipelineOut = &pcmOut;

  if (hasWavInfo && wavInfo.channels == 1) {
    monoExpander.setOutput(pcmOut);
    int bps = wavInfo.bits_per_sample;
    if (bps == 8) bps = 16;
    if (bps == 24) bps = 32;
    monoExpander.setBitsPerSample(bps);
    pipelineOut = &monoExpander;
    self->log(INFO, "🛜🎵 Expanding mono WAV to stereo for A2DP");
  }

  ResampleStream* resampler = nullptr;
  bool resampleActive = false;
  Print* pcmSink = pipelineOut;

  if (hasWavInfo && wavInfo.sample_rate > 0 &&
      wavInfo.sample_rate != kA2dpTargetSampleRate) {
    AudioInfo from;
    from.sample_rate = wavInfo.sample_rate;
    from.channels = wavInfo.channels > 0 ? wavInfo.channels : 2;
    from.bits_per_sample = wavInfo.bits_per_sample;
    if (from.bits_per_sample == 8) from.bits_per_sample = 16;
    if (from.bits_per_sample == 24) from.bits_per_sample = 32;

    resampler = new ResampleStream();
    if (resampler) {
      resampler->setOutput(*pipelineOut);
      if (resampler->begin(from, kA2dpTargetSampleRate)) {
        pcmSink = resampler;
        resampleActive = true;
        self->log(INFO, "🛜🎵 Resampling %d Hz → %d Hz", wavInfo.sample_rate,
                  kA2dpTargetSampleRate);
      } else {
        self->log(
            WARNING,
            "🛜🎵 Resampler init failed for %d Hz input; using passthrough",
            wavInfo.sample_rate);
        delete resampler;
        resampler = nullptr;
      }
    }
  }

  decoder->setOutput(*pcmSink);
  decoder->begin();

  s_a2dp_source.set_data_callback(wavDataCb);
  self->log(INFO, "🛜🎵 WAV playback started: %s", path.c_str());

  uint8_t* readBuf = (uint8_t*)malloc(512);
  if (!readBuf) {
    self->log(ERROR, "🛜🎵 Failed to allocate WAV read buffer");
    self->btAudioPlaying = false;
  }

  while (self->btAudioPlaying && readBuf) {
    if (self->btAudioPaused) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    size_t n = fread(readBuf, 1, 512, f);
    if (n == 0) {
      self->btAudioPlaying = false;
      break;
    }
    decoder->write(readBuf, n);
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (resampleActive && resampler) {
    resampler->flush();
  }

  if (readBuf) free(readBuf);
  decoder->end();
  delete decoder;
  if (resampler) delete resampler;
  fclose(f);

  self->log(INFO, "🛜🎵 WAV playback finished");
  self->btAudioPlaying = false;
  self->btAudioPaused = false;
  wavPaused = false;
  if (self->a2dp_source != nullptr) {
    s_a2dp_source.set_data_callback(silentDataCb);
  }

  self->btAudioTask = nullptr;
  vTaskDelete(nullptr);
}

void ESPWiFi::startBluetoothWavPlayback(const char* path) {
  if (a2dp_source == nullptr) {
    log(WARNING, "🛜🎵 Cannot play WAV: Bluetooth not started");
    return;
  }
  if (btAudioTask != nullptr) {
    log(INFO, "🛜🎵 Cleaning previous WAV task before restart");
    stopBluetoothWavPlayback();
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
  btAudioPaused = false;
  wavPaused = false;
  wavBuf.reset();

  BaseType_t ok = xTaskCreatePinnedToCore(wavDecoderTaskFunc, "wavdec", 12288,
                                          this, 5, &btAudioTask, 1);
  if (ok != pdPASS) {
    log(ERROR, "🛜🎵 Failed to create WAV decoder task");
    btAudioPlaying = false;
    btAudioTask = nullptr;
  }
}

void ESPWiFi::pauseBluetoothWavPlayback() {
  if (!btAudioPlaying || btAudioPaused) return;
  btAudioPaused = true;
  wavPaused = true;
  wavBuf.reset();
  log(INFO, "🛜🎵 WAV playback paused");
}

void ESPWiFi::resumeBluetoothWavPlayback() {
  if (!btAudioPlaying || !btAudioPaused) return;
  btAudioPaused = false;
  wavPaused = false;
  log(INFO, "🛜🎵 WAV playback resumed");
}

void ESPWiFi::toggleBluetoothWavPause() {
  if (!btAudioPlaying) return;
  if (btAudioPaused) {
    resumeBluetoothWavPlayback();
  } else {
    pauseBluetoothWavPlayback();
  }
}

void ESPWiFi::stopBluetoothWavPlayback() {
  if (!btAudioPlaying && btAudioTask == nullptr) return;
  btAudioPlaying = false;
  btAudioPaused = false;
  wavPaused = false;

  TaskHandle_t task = btAudioTask;
  btAudioTask = nullptr;
  if (task != nullptr) {
    vTaskDelete(task);
  }

  if (a2dp_source != nullptr) {
    s_a2dp_source.set_data_callback(silentDataCb);
  }
  wavBuf.reset();
  log(INFO, "🛜🎵 WAV playback stopped");
}

#endif  // CONFIG_BT_A2DP_ENABLE
#endif  // ESPWiFi_BLUETOOTH_WAV_H
