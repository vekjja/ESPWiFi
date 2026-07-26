#ifdef ESPWiFi_DAC_ENABLED

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>

#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/CoreAudio/AudioAnalog/AnalogAudioStream.h"
#include "AudioTools/CoreAudio/ResampleStream.h"
#include "ESPWiFi.h"
#include "driver/dac_continuous.h"
#include "freertos/stream_buffer.h"

using namespace audio_tools;

namespace {

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

class VolumePrint : public Print {
 public:
  void setVolume(float volume) { volume_ = volume; }
  void setOutput(Print& out) { p_out = &out; }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t* data, size_t len) override {
    if (p_out == nullptr || data == nullptr || len < 2) return 0;

    constexpr size_t kChunkSamples = 256;
    int16_t out_buf[kChunkSamples];
    size_t out_samples = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
      int16_t sample =
          (int16_t)((uint16_t)data[i] | ((uint16_t)data[i + 1] << 8));
      int32_t scaled = (int32_t)(sample * volume_);
      if (scaled > 32767) scaled = 32767;
      if (scaled < -32768) scaled = -32768;
      out_buf[out_samples++] = (int16_t)scaled;

      if (out_samples == kChunkSamples) {
        p_out->write((const uint8_t*)out_buf, out_samples * sizeof(int16_t));
        out_samples = 0;
      }
    }

    if (out_samples > 0) {
      p_out->write((const uint8_t*)out_buf, out_samples * sizeof(int16_t));
    }
    return len;
  }

 protected:
  Print* p_out = nullptr;
  float volume_ = 1.0f;
};

class StereoToMonoPrint : public Print {
 public:
  void setOutput(Print& out) { p_out = &out; }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t* data, size_t len) override {
    if (p_out == nullptr || data == nullptr || len < 4) return 0;

    constexpr size_t kOutChunkSize = 512;
    uint8_t out_buf[kOutChunkSize];
    size_t out_pos = 0;

    for (size_t i = 0; i + 3 < len; i += 4) {
      int16_t left =
          (int16_t)((uint16_t)data[i] | ((uint16_t)data[i + 1] << 8));
      int16_t right =
          (int16_t)((uint16_t)data[i + 2] | ((uint16_t)data[i + 3] << 8));
      int16_t mono = (int16_t)(((int32_t)left + (int32_t)right) / 2);

      if (out_pos + 2 > kOutChunkSize) {
        p_out->write(out_buf, out_pos);
        out_pos = 0;
      }
      out_buf[out_pos++] = (uint8_t)(mono & 0xFF);
      out_buf[out_pos++] = (uint8_t)((mono >> 8) & 0xFF);
    }

    if (out_pos > 0) {
      p_out->write(out_buf, out_pos);
    }
    return len;
  }

 protected:
  Print* p_out = nullptr;
};

static std::string resolveAudioFilePath(ESPWiFi* self,
                                        const std::string& path) {
  if (path.empty()) {
    return path;
  }
  if (path.rfind(self->sdMountPoint, 0) == 0 ||
      path.rfind(self->lfsMountPoint, 0) == 0) {
    return path;
  }
  if (path[0] == '/') {
    return self->lfsMountPoint + path;
  }
  return self->resolvePathOnSD(path);
}

static bool configureDacOutput(AnalogConfig& dacCfg, int outputPin) {
  dacCfg.rx_tx_mode = TX_MODE;
  dacCfg.bits_per_sample = 16;
  dacCfg.channels = 1;

  if (outputPin == ESPWiFi::ESPWiFi_DAC_PIN_2) {
    dacCfg.dac_mono_channel = DAC_CHANNEL_MASK_CH1;
    return true;
  }
  if (outputPin == ESPWiFi::ESPWiFi_DAC_PIN_1) {
    dacCfg.dac_mono_channel = DAC_CHANNEL_MASK_CH0;
    return true;
  }
  return false;
}

struct AudioPlaybackContext {
  ESPWiFi* self = nullptr;
  std::string path;
  float volume = 1.0f;
  int outputPin = -1;
  int pttPin = -1;
  bool deleteAfterPlay = false;
};

struct StreamingAudioContext {
  ESPWiFi* self = nullptr;
  float volume = 1.0f;
  int outputPin = -1;
  int pttPin = -1;
  ESPWiFi::AudioStreamProvider provider;
};

struct StreamingDownloadState {
  ESPWiFi* self = nullptr;
  ESPWiFi::AudioStreamProvider provider;
  StreamBufferHandle_t buffer = nullptr;
  volatile bool* downloadDone = nullptr;
  volatile bool ok = false;
};

static void streamingDownloadTask(void* param) {
  auto* state = static_cast<StreamingDownloadState*>(param);

  state->ok = state->provider([&](const uint8_t* data, size_t len) {
    if (!state->self->audioPlaying || data == nullptr || len == 0) {
      return false;
    }

    size_t offset = 0;
    while (offset < len) {
      if (!state->self->audioPlaying) {
        return false;
      }

      const size_t sent = xStreamBufferSend(state->buffer, data + offset,
                                            len - offset, pdMS_TO_TICKS(500));
      if (sent == 0) {
        continue;
      }
      offset += sent;
    }

    state->self->feedWatchDog();
    return true;
  });

  if (state->downloadDone != nullptr) {
    *state->downloadDone = true;
  }

  vTaskDelete(nullptr);
}

static bool waitForStreamPrebuffer(ESPWiFi* self, StreamBufferHandle_t buffer,
                                   volatile bool* downloadDone,
                                   size_t minBytes) {
  while (self->audioPlaying) {
    if (xStreamBufferBytesAvailable(buffer) >= minBytes) {
      return true;
    }
    if (downloadDone != nullptr && *downloadDone) {
      return xStreamBufferBytesAvailable(buffer) > 0;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}

static StreamBufferHandle_t createStreamBuffer(size_t preferredBytes) {
  constexpr size_t kFallbackBytes = 64 * 1024;
  StreamBufferHandle_t buffer = xStreamBufferCreate(preferredBytes, 1);
  if (buffer == nullptr && preferredBytes > kFallbackBytes) {
    buffer = xStreamBufferCreate(kFallbackBytes, 1);
  }
  return buffer;
}

static bool pumpWavDecoderStream(ESPWiFi* self, WAVDecoder& decoder, int pttPin,
                                 StreamBufferHandle_t buffer,
                                 volatile bool* downloadDone) {
  uint8_t* readBuf = static_cast<uint8_t*>(malloc(1024));
  if (!readBuf) {
    self->log(ERROR, "🔊 Failed to allocate read buffer");
    return false;
  }

  auto receiveChunk = [&]() -> size_t {
    return xStreamBufferReceive(buffer, readBuf, 1024, pdMS_TO_TICKS(100));
  };

  while (self->audioPlaying && decoder.audioInfoEx().sample_rate == 0) {
    const size_t n = receiveChunk();
    if (n == 0) {
      if (downloadDone != nullptr && *downloadDone &&
          xStreamBufferBytesAvailable(buffer) == 0) {
        free(readBuf);
        return false;
      }
      vTaskDelay(1);
      continue;
    }
    decoder.write(readBuf, n);
  }

  if (self->audioPlaying && pttPin != -1) {
    self->setGPIO(pttPin, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    self->log(INFO, "🔊 PTT keyed on GPIO %d", pttPin);
  }

  while (self->audioPlaying) {
    const size_t n = receiveChunk();
    if (n == 0) {
      if (downloadDone != nullptr && *downloadDone &&
          xStreamBufferBytesAvailable(buffer) == 0) {
        break;
      }
      vTaskDelay(1);
      continue;
    }
    decoder.write(readBuf, n);
    vTaskDelay(1);
  }

  if (pttPin != -1) {
    self->setGPIO(pttPin, 0);
  }

  free(readBuf);
  return true;
}

static void releaseDac(AnalogAudioStream& dac) {
  dac.end();
  vTaskDelay(pdMS_TO_TICKS(50));
}

static bool pumpWavDecoder(
    ESPWiFi* self, WAVDecoder& decoder, ResampleStream* resampler, int pttPin,
    const std::function<size_t(uint8_t*, size_t)>& readFn) {
  uint8_t* readBuf = (uint8_t*)malloc(512);
  if (!readBuf) {
    self->log(ERROR, "🔊 Failed to allocate read buffer");
    return false;
  }

  if (self->audioPlaying && pttPin != -1) {
    self->setGPIO(pttPin, 1);
    self->log(INFO, "🔊 PTT keyed on GPIO %d", pttPin);
  }

  while (self->audioPlaying && readBuf &&
         decoder.audioInfoEx().sample_rate == 0) {
    const size_t n = readFn(readBuf, 512);
    if (n == 0) {
      break;
    }
    decoder.write(readBuf, n);
    vTaskDelay(1);
  }

  while (self->audioPlaying && readBuf) {
    const size_t n = readFn(readBuf, 512);
    if (n == 0) {
      break;
    }
    decoder.write(readBuf, n);
    vTaskDelay(1);
  }

  if (resampler) {
    resampler->flush();
  }

  if (pttPin != -1) {
    self->setGPIO(pttPin, 0);
  }

  free(readBuf);
  return true;
}

static void streamingAudioPlaybackTask(void* param) {
  StreamingAudioContext ctx = *static_cast<StreamingAudioContext*>(param);
  delete static_cast<StreamingAudioContext*>(param);

  ESPWiFi* self = ctx.self;
  const int pttPin = ctx.pttPin;

  constexpr size_t kStreamPrebufferBytes = 16 * 1024;
  StreamBufferHandle_t streamBuffer = createStreamBuffer(128 * 1024);
  if (streamBuffer == nullptr) {
    self->log(ERROR, "🔊 Failed to allocate stream buffer");
    self->audioPlaying = false;
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  self->log(INFO, "🔊 Stream buffer: %u bytes free capacity",
            static_cast<unsigned>(xStreamBufferSpacesAvailable(streamBuffer) +
                                  xStreamBufferBytesAvailable(streamBuffer)));

  volatile bool downloadDone = false;
  StreamingDownloadState downloadState;
  downloadState.self = self;
  downloadState.provider = std::move(ctx.provider);
  downloadState.buffer = streamBuffer;
  downloadState.downloadDone = &downloadDone;

  if (xTaskCreatePinnedToCore(streamingDownloadTask, "tts-download", 12288,
                              &downloadState, 6, nullptr, 0) != pdPASS) {
    self->log(ERROR, "🔊 Failed to create TTS download task");
    vStreamBufferDelete(streamBuffer);
    self->audioPlaying = false;
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  if (!waitForStreamPrebuffer(self, streamBuffer, &downloadDone,
                              kStreamPrebufferBytes)) {
    self->log(ERROR, "🔊 Streaming prebuffer failed");
    self->audioPlaying = false;
    vStreamBufferDelete(streamBuffer);
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  // 🤖 OpenAI TTS WAV responses are 24 kHz mono PCM16.
  AnalogAudioStream dac;
  auto dacCfg = dac.defaultConfig(TX_MODE);
  if (!configureDacOutput(dacCfg, ctx.outputPin)) {
    self->log(ERROR, "🔊 Invalid DAC output pin: %d", ctx.outputPin);
    self->audioPlaying = false;
    vStreamBufferDelete(streamBuffer);
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  dacCfg.sample_rate = 24000;

  if (!dac.begin(dacCfg)) {
    self->log(ERROR, "🔊 Failed to start DAC on GPIO %d", ctx.outputPin);
    self->audioPlaying = false;
    vStreamBufferDelete(streamBuffer);
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  VolumePrint volumeOut;
  volumeOut.setVolume(ctx.volume);
  volumeOut.setOutput(dac);

  WAVDecoder* decoder = new WAVDecoder();
  if (!decoder) {
    self->log(ERROR, "🔊 Failed to allocate WAV decoder");
    releaseDac(dac);
    self->audioPlaying = false;
    vStreamBufferDelete(streamBuffer);
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  decoder->setOutput(volumeOut);
  decoder->begin();

  self->log(INFO, "🔊 Streaming WAV playback started (GPIO %d, volume %.2f)",
            ctx.outputPin, ctx.volume);

  const bool playbackOk =
      pumpWavDecoderStream(self, *decoder, pttPin, streamBuffer, &downloadDone);

  decoder->end();
  delete decoder;
  releaseDac(dac);
  vStreamBufferDelete(streamBuffer);

  if (!downloadState.ok || !playbackOk) {
    self->log(ERROR, "🔊 Streaming WAV playback failed");
  } else {
    self->log(INFO, "🔊 Streaming WAV playback finished");
  }

  self->audioPlaying = false;
  self->audioTask = nullptr;
  vTaskDelete(nullptr);
}

static void audioPlaybackTask(void* param) {
  AudioPlaybackContext ctx = *static_cast<AudioPlaybackContext*>(param);
  delete static_cast<AudioPlaybackContext*>(param);

  ESPWiFi* self = ctx.self;
  const std::string& path = ctx.path;
  const float volume = ctx.volume;
  const int outputPin = ctx.outputPin;
  const int pttPin = ctx.pttPin;
  const bool deleteAfterPlay = ctx.deleteAfterPlay;

  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    self->log(ERROR, "🔊 Failed to open audio file: %s", path.c_str());
    self->audioPlaying = false;
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  WAVAudioInfo wavInfo;
  bool hasWavInfo = probeWavFormat(f, wavInfo);
  if (hasWavInfo) {
    self->log(INFO, "🔊 WAV format: %d Hz, %d ch, %d bit", wavInfo.sample_rate,
              wavInfo.channels, wavInfo.bits_per_sample);
  } else {
    self->log(WARNING, "🔊 Could not parse WAV format; using defaults");
  }

  AnalogAudioStream dac;
  auto dacCfg = dac.defaultConfig(TX_MODE);
  if (!configureDacOutput(dacCfg, outputPin)) {
    self->log(ERROR, "🔊 Invalid DAC output pin: %d", outputPin);
    fclose(f);
    self->audioPlaying = false;
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  dacCfg.sample_rate =
      hasWavInfo && wavInfo.sample_rate > 0 ? wavInfo.sample_rate : 44100;

  if (!dac.begin(dacCfg)) {
    self->log(ERROR, "🔊 Failed to start DAC on GPIO %d", outputPin);
    fclose(f);
    self->audioPlaying = false;
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  VolumePrint volumeOut;
  volumeOut.setVolume(volume);
  volumeOut.setOutput(dac);

  WAVDecoder* decoder = new WAVDecoder();
  if (!decoder) {
    self->log(ERROR, "🔊 Failed to allocate WAV decoder");
    releaseDac(dac);
    fclose(f);
    self->audioPlaying = false;
    self->audioTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  StereoToMonoPrint stereoDownmix;
  Print* pipelineOut = &volumeOut;

  if (hasWavInfo && wavInfo.channels > 1) {
    stereoDownmix.setOutput(volumeOut);
    pipelineOut = &stereoDownmix;
    self->log(INFO, "🔊 Downmixing stereo WAV to mono for DAC GPIO %d",
              outputPin);
  }

  ResampleStream* resampler = nullptr;
  Print* pcmSink = pipelineOut;
  const int targetRate = dacCfg.sample_rate;

  if (hasWavInfo && wavInfo.sample_rate > 0 &&
      wavInfo.sample_rate != targetRate) {
    AudioInfo from;
    from.sample_rate = wavInfo.sample_rate;
    from.channels = wavInfo.channels > 0 ? wavInfo.channels : 2;
    from.bits_per_sample = wavInfo.bits_per_sample;
    if (from.bits_per_sample == 8) from.bits_per_sample = 16;
    if (from.bits_per_sample == 24) from.bits_per_sample = 32;

    resampler = new ResampleStream();
    if (resampler) {
      resampler->setOutput(*pipelineOut);
      if (resampler->begin(from, targetRate)) {
        pcmSink = resampler;
        self->log(INFO, "🔊 Resampling %d Hz → %d Hz", wavInfo.sample_rate,
                  targetRate);
      } else {
        self->log(WARNING, "🔊 Resampler init failed; using passthrough");
        delete resampler;
        resampler = nullptr;
      }
    }
  }

  decoder->setOutput(*pcmSink);
  decoder->begin();

  self->log(INFO, "🔊 DAC playback started: %s (GPIO %d, volume %.2f)",
            path.c_str(), outputPin, volume);

  pumpWavDecoder(self, *decoder, resampler, pttPin,
                 [f](uint8_t* buffer, size_t maxLen) {
                   return fread(buffer, 1, maxLen, f);
                 });

  decoder->end();
  delete decoder;
  if (resampler) delete resampler;
  releaseDac(dac);
  fclose(f);

  if (deleteAfterPlay) {
    if (remove(path.c_str()) == 0) {
      self->log(INFO, "🔊 Deleted audio file: %s", path.c_str());
    } else {
      self->log(WARNING, "🔊 Failed to delete audio file: %s", path.c_str());
    }
  }

  self->log(INFO, "🔊 DAC playback finished");
  self->audioPlaying = false;
  self->audioTask = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace

void ESPWiFi::playAudio(const std::string& path, float volume, int outputPin,
                        bool deleteAfterPlay) {
  if (outputPin != ESPWiFi_DAC_PIN_1 && outputPin != ESPWiFi_DAC_PIN_2) {
    log(WARNING, "🔊 Invalid DAC pin %d (use GPIO %d or %d)", outputPin,
        ESPWiFi_DAC_PIN_1, ESPWiFi_DAC_PIN_2);
    return;
  }

  if (volume < 0.0f) volume = 0.0f;
  if (volume > 1.0f) volume = 1.0f;

  if (audioTask != nullptr) {
    stopAudioPlayback();
  }

  audioFilePath = resolveAudioFilePath(this, path);
  if (audioFilePath.empty()) {
    log(WARNING, "🔊 No audio file path provided");
    return;
  }

  FILE* probe = fopen(audioFilePath.c_str(), "rb");
  if (!probe) {
    log(ERROR, "🔊 Audio file not found: %s", audioFilePath.c_str());
    return;
  }
  fclose(probe);

  audioOutputPin = outputPin;
  audioPlaying = true;

  auto* ctx = new AudioPlaybackContext{
      this, audioFilePath, volume, outputPin, audioPttPin, deleteAfterPlay};
  BaseType_t ok = xTaskCreatePinnedToCore(audioPlaybackTask, "dac-audio", 12288,
                                          ctx, 5, &audioTask, 1);
  if (ok != pdPASS) {
    log(ERROR, "🔊 Failed to create DAC playback task");
    delete ctx;
    audioPlaying = false;
    audioTask = nullptr;
  }
}

void ESPWiFi::playStreamingWav(float volume, int outputPin,
                               AudioStreamProvider provider) {
  if (outputPin != ESPWiFi_DAC_PIN_1 && outputPin != ESPWiFi_DAC_PIN_2) {
    log(WARNING, "🔊 Invalid DAC pin %d (use GPIO %d or %d)", outputPin,
        ESPWiFi_DAC_PIN_1, ESPWiFi_DAC_PIN_2);
    return;
  }

  if (volume < 0.0f) volume = 0.0f;
  if (volume > 1.0f) volume = 1.0f;

  if (audioTask != nullptr) {
    stopAudioPlayback();
  }

  if (!provider) {
    log(WARNING, "🔊 No streaming audio provider");
    return;
  }

  audioOutputPin = outputPin;
  audioPlaying = true;

  auto* ctx = new StreamingAudioContext{this, volume, outputPin, audioPttPin,
                                        std::move(provider)};
  BaseType_t ok =
      xTaskCreatePinnedToCore(streamingAudioPlaybackTask, "dac-audio-stream",
                              16384, ctx, 5, &audioTask, 1);
  if (ok != pdPASS) {
    log(ERROR, "🔊 Failed to create streaming DAC playback task");
    delete ctx;
    audioPlaying = false;
    audioTask = nullptr;
  }
}

void ESPWiFi::stopAudioPlayback() {
  if (!audioPlaying && audioTask == nullptr) return;

  audioPlaying = false;

  for (int i = 0; i < 200 && audioTask != nullptr; ++i) {
    feedWatchDog(10);
  }

  if (audioTask != nullptr) {
    vTaskDelete(audioTask);
    audioTask = nullptr;
  }

  if (audioPttPin != -1) {
    setGPIO(audioPttPin, 0);
  }

  log(INFO, "🔊 DAC playback stopped");
}

#endif  // ESPWiFi_DAC_ENABLED
