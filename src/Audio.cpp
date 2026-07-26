#ifdef ESPWiFi_DAC_ENABLED

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>

#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/CoreAudio/AudioAnalog/AnalogAudioStream.h"
#include "AudioTools/CoreAudio/AudioFilter/Filter.h"
#include "AudioTools/CoreAudio/ResampleStream.h"
#include "ESPWiFi.h"
#include "driver/dac_continuous.h"
#include "esp_heap_caps.h"
#include "freertos/stream_buffer.h"

using namespace audio_tools;

namespace {

// OpenAI TTS returns 24 kHz mono PCM16 WAV.
static constexpr int kOpenAiTtsSampleRate = 24000;

// Voice-band shaping for DAC -> radio mic input.
static constexpr float kRadioMicHpfHz = 300.0f;
static constexpr float kRadioMicLpfHz = 3000.0f;

static constexpr size_t kFileReadChunkBytes = 512;
static constexpr size_t kStreamReadChunkBytes = 2048;
static constexpr size_t kStreamPrebufferBytes = 16 * 1024;
static constexpr size_t kMinPrebufferBytes = 4096;

static constexpr size_t kStreamBufferTrySizes[] = {256 * 1024, 128 * 1024,
                                                   64 * 1024, 32 * 1024};

// ---- WAV helpers ------------------------------------------------------------

static uint16_t readLe16(const uint8_t* ptr) {
  return static_cast<uint16_t>(ptr[0] | (static_cast<uint16_t>(ptr[1]) << 8));
}

static uint32_t readLe32(const uint8_t* ptr) {
  return static_cast<uint32_t>(ptr[0]) |
         (static_cast<uint32_t>(ptr[1]) << 8) |
         (static_cast<uint32_t>(ptr[2]) << 16) |
         (static_cast<uint32_t>(ptr[3]) << 24);
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

    const uint32_t chunkSize = readLe32(chunkHeader + 4);
    const bool isFmt = memcmp(chunkHeader, "fmt ", 4) == 0;
    const bool isData = memcmp(chunkHeader, "data", 4) == 0;

    if (isFmt) {
      if (chunkSize < 16) return false;
      uint8_t fmt[16];
      if (fread(fmt, 1, sizeof(fmt), f) != sizeof(fmt)) return false;

      info.format = static_cast<AudioFormat>(readLe16(fmt + 0));
      info.channels = static_cast<int>(readLe16(fmt + 2));
      info.sample_rate = static_cast<int>(readLe32(fmt + 4));
      info.byte_rate = static_cast<int>(readLe32(fmt + 8));
      info.block_align = static_cast<int>(readLe16(fmt + 12));
      info.bits_per_sample = static_cast<int>(readLe16(fmt + 14));
      info.is_valid = true;

      const long remaining = static_cast<long>(chunkSize) - static_cast<long>(sizeof(fmt));
      if (remaining > 0 && fseek(f, remaining, SEEK_CUR) != 0) return false;
      gotFmt = true;
    } else {
      if (isData && gotFmt) {
        return fseek(f, 0, SEEK_SET) == 0;
      }
      if (chunkSize > 0 && fseek(f, static_cast<long>(chunkSize), SEEK_CUR) != 0) {
        return false;
      }
    }

    if ((chunkSize & 1U) != 0U && fseek(f, 1, SEEK_CUR) != 0) {
      return false;
    }
  }

  return fseek(f, 0, SEEK_SET) == 0 && gotFmt;
}

// ---- PCM pipeline (volume + radio band-pass) --------------------------------

class RadioMicPrint : public Print {
 public:
  void begin(int sampleRate) {
    sample_rate_ = sampleRate > 0 ? sampleRate : kOpenAiTtsSampleRate;
    hpf_.begin(kRadioMicHpfHz, static_cast<float>(sample_rate_));
    lpf_.begin(kRadioMicLpfHz, static_cast<float>(sample_rate_));
  }

  void setVolume(float volume) { volume_ = volume; }
  void setOutput(Print& out) { p_out = &out; }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t* data, size_t len) override {
    if (p_out == nullptr || data == nullptr || len < 2) return 0;

    constexpr size_t kChunkSamples = 256;
    int16_t out_buf[kChunkSamples];
    size_t out_samples = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
      const int16_t sample = static_cast<int16_t>(
          static_cast<uint16_t>(data[i]) |
          (static_cast<uint16_t>(data[i + 1]) << 8));

      float filtered = static_cast<float>(sample) / 32768.0f;
      filtered = hpf_.process(filtered);
      filtered = lpf_.process(filtered);

      int32_t scaled = static_cast<int32_t>(filtered * volume_ * 32767.0f);
      if (scaled > 32767) scaled = 32767;
      if (scaled < -32768) scaled = -32768;
      out_buf[out_samples++] = static_cast<int16_t>(scaled);

      if (out_samples == kChunkSamples) {
        p_out->write(reinterpret_cast<const uint8_t*>(out_buf),
                     out_samples * sizeof(int16_t));
        out_samples = 0;
      }
    }

    if (out_samples > 0) {
      p_out->write(reinterpret_cast<const uint8_t*>(out_buf),
                   out_samples * sizeof(int16_t));
    }
    return len;
  }

 private:
  Print* p_out = nullptr;
  float volume_ = 1.0f;
  int sample_rate_ = kOpenAiTtsSampleRate;
  HighPassFilter<float> hpf_;
  LowPassFilter<float> lpf_;
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
      const int16_t left = static_cast<int16_t>(
          static_cast<uint16_t>(data[i]) |
          (static_cast<uint16_t>(data[i + 1]) << 8));
      const int16_t right = static_cast<int16_t>(
          static_cast<uint16_t>(data[i + 2]) |
          (static_cast<uint16_t>(data[i + 3]) << 8));
      const int16_t mono =
          static_cast<int16_t>((static_cast<int32_t>(left) +
                                static_cast<int32_t>(right)) /
                               2);

      if (out_pos + 2 > kOutChunkSize) {
        p_out->write(out_buf, out_pos);
        out_pos = 0;
      }
      out_buf[out_pos++] = static_cast<uint8_t>(mono & 0xFF);
      out_buf[out_pos++] = static_cast<uint8_t>((mono >> 8) & 0xFF);
    }

    if (out_pos > 0) {
      p_out->write(out_buf, out_pos);
    }
    return len;
  }

 private:
  Print* p_out = nullptr;
};

// ---- DAC / playback helpers -------------------------------------------------

static std::string resolveAudioFilePath(ESPWiFi* self, const std::string& path) {
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

static float clampVolume(float volume) {
  if (volume < 0.0f) return 0.0f;
  if (volume > 1.0f) return 1.0f;
  return volume;
}

static bool isValidDacPin(int outputPin) {
  return outputPin == ESPWiFi::ESPWiFi_DAC_PIN_1 ||
         outputPin == ESPWiFi::ESPWiFi_DAC_PIN_2;
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

static void releaseDac(AnalogAudioStream& dac) {
  dac.end();
  vTaskDelay(pdMS_TO_TICKS(50));
}

static void finishPlaybackTask(ESPWiFi* self) {
  self->audioPlaying = false;
  self->audioTask = nullptr;
  vTaskDelete(nullptr);
}

static void setPtt(ESPWiFi* self, int pttPin, bool keyed) {
  if (pttPin == -1) return;
  self->setGPIO(pttPin, keyed ? "high" : "low");
  if (keyed) {
    self->log(INFO, "🔊 PTT keyed on GPIO %d", pttPin);
  }
}

struct DacPlayback {
  AnalogAudioStream dac;
  RadioMicPrint radio;
  WAVDecoder* decoder = nullptr;

  bool begin(ESPWiFi* self, int outputPin, int sampleRate, float volume) {
    auto dacCfg = dac.defaultConfig(TX_MODE);
    if (!configureDacOutput(dacCfg, outputPin)) {
      self->log(ERROR, "🔊 Invalid DAC output pin: %d", outputPin);
      return false;
    }

    dacCfg.sample_rate = sampleRate;
    if (!dac.begin(dacCfg)) {
      self->log(ERROR, "🔊 Failed to start DAC on GPIO %d", outputPin);
      return false;
    }

    radio.begin(sampleRate);
    radio.setVolume(volume);
    radio.setOutput(dac);
    return true;
  }

  bool startDecoder(ESPWiFi* self, Print& sink) {
    decoder = new WAVDecoder();
    if (decoder == nullptr) {
      self->log(ERROR, "🔊 Failed to allocate WAV decoder");
      return false;
    }

    decoder->setOutput(sink);
    decoder->begin();
    return true;
  }

  void end() {
    if (decoder != nullptr) {
      decoder->end();
      delete decoder;
      decoder = nullptr;
    }
    releaseDac(dac);
  }
};

struct WavChunkReader {
  std::function<size_t(uint8_t*, size_t)> read;
  std::function<bool()> waitForMore;
};

static bool pumpWavDecoder(ESPWiFi* self, WAVDecoder& decoder, int pttPin,
                           ResampleStream* resampler,
                           const WavChunkReader& reader, size_t chunkSize) {
  uint8_t* readBuf = static_cast<uint8_t*>(malloc(chunkSize));
  if (readBuf == nullptr) {
    self->log(ERROR, "🔊 Failed to allocate read buffer");
    return false;
  }

  setPtt(self, pttPin, true);

  while (self->audioPlaying && decoder.audioInfoEx().sample_rate == 0) {
    const size_t n = reader.read(readBuf, chunkSize);
    if (n == 0) {
      if (reader.waitForMore && reader.waitForMore()) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      setPtt(self, pttPin, false);
      free(readBuf);
      return false;
    }
    decoder.write(readBuf, n);
  }

  while (self->audioPlaying) {
    const size_t n = reader.read(readBuf, chunkSize);
    if (n == 0) {
      if (reader.waitForMore && reader.waitForMore()) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      break;
    }
    decoder.write(readBuf, n);
  }

  if (resampler != nullptr) {
    resampler->flush();
  }

  setPtt(self, pttPin, false);
  free(readBuf);
  return true;
}

static ResampleStream* maybeCreateResampler(ESPWiFi* self, Print& sink,
                                            const WAVAudioInfo& wavInfo,
                                            int targetRate) {
  if (!wavInfo.is_valid || wavInfo.sample_rate <= 0 ||
      wavInfo.sample_rate == targetRate) {
    return nullptr;
  }

  AudioInfo from;
  from.sample_rate = wavInfo.sample_rate;
  from.channels = wavInfo.channels > 0 ? wavInfo.channels : 2;
  from.bits_per_sample = wavInfo.bits_per_sample;
  if (from.bits_per_sample == 8) from.bits_per_sample = 16;
  if (from.bits_per_sample == 24) from.bits_per_sample = 32;

  auto* resampler = new ResampleStream();
  if (resampler == nullptr) {
    return nullptr;
  }

  resampler->setOutput(sink);
  if (!resampler->begin(from, targetRate)) {
    self->log(WARNING, "🔊 Resampler init failed; using passthrough");
    delete resampler;
    return nullptr;
  }

  self->log(INFO, "🔊 Resampling %d Hz → %d Hz", wavInfo.sample_rate,
            targetRate);
  return resampler;
}

// ---- Streaming buffer -------------------------------------------------------

static bool writeToStreamBuffer(ESPWiFi* self, StreamBufferHandle_t buffer,
                              const uint8_t* data, size_t len) {
  size_t offset = 0;
  while (offset < len) {
    if (!self->audioPlaying) {
      return false;
    }

    const size_t sent = xStreamBufferSend(buffer, data + offset, len - offset,
                                          pdMS_TO_TICKS(1000));
    if (sent == 0) {
      if (!self->audioPlaying) {
        return false;
      }
      self->feedWatchDog();
      continue;
    }
    offset += sent;
  }

  self->feedWatchDog();
  return true;
}

static StreamBufferHandle_t createStreamBuffer(size_t* outCapacityBytes) {
  for (size_t size : kStreamBufferTrySizes) {
    StreamBufferHandle_t buffer = xStreamBufferCreate(size, 1);
    if (buffer != nullptr) {
      if (outCapacityBytes != nullptr) {
        *outCapacityBytes = size;
      }
      return buffer;
    }
  }

  if (outCapacityBytes != nullptr) {
    *outCapacityBytes = 0;
  }
  return nullptr;
}

static size_t streamPrebufferForCapacity(size_t capacityBytes) {
  size_t prebuffer = kStreamPrebufferBytes;
  if (prebuffer > capacityBytes / 4) {
    prebuffer = capacityBytes / 4;
  }
  if (prebuffer < kMinPrebufferBytes) {
    prebuffer = kMinPrebufferBytes;
  }
  return prebuffer;
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

// ---- Task contexts ----------------------------------------------------------

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
    return writeToStreamBuffer(state->self, state->buffer, data, len);
  });

  if (state->downloadDone != nullptr) {
    *state->downloadDone = true;
  }

  vTaskDelete(nullptr);
}

static void streamingAudioPlaybackTask(void* param) {
  StreamingAudioContext ctx = *static_cast<StreamingAudioContext*>(param);
  delete static_cast<StreamingAudioContext*>(param);

  ESPWiFi* self = ctx.self;
  const int pttPin = ctx.pttPin;

  size_t streamBufferBytes = 0;
  StreamBufferHandle_t streamBuffer = createStreamBuffer(&streamBufferBytes);
  if (streamBuffer == nullptr) {
    self->log(ERROR,
              "🔊 Failed to allocate stream buffer (free=%u, largest=%u)",
              static_cast<unsigned>(esp_get_free_heap_size()),
              static_cast<unsigned>(
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    finishPlaybackTask(self);
    return;
  }

  const size_t prebufferBytes = streamPrebufferForCapacity(streamBufferBytes);
  self->log(INFO, "🔊 Stream buffer: %u bytes (prebuffer %u)",
            static_cast<unsigned>(streamBufferBytes),
            static_cast<unsigned>(prebufferBytes));

  volatile bool downloadDone = false;
  StreamingDownloadState downloadState{
      self, std::move(ctx.provider), streamBuffer, &downloadDone, false};

  if (xTaskCreatePinnedToCore(streamingDownloadTask, "tts-download", 12288,
                              &downloadState, 6, nullptr, 0) != pdPASS) {
    self->log(ERROR, "🔊 Failed to create TTS download task");
    vStreamBufferDelete(streamBuffer);
    finishPlaybackTask(self);
    return;
  }

  if (!waitForStreamPrebuffer(self, streamBuffer, &downloadDone,
                              prebufferBytes)) {
    self->log(ERROR, "🔊 Streaming prebuffer failed");
    vStreamBufferDelete(streamBuffer);
    finishPlaybackTask(self);
    return;
  }

  DacPlayback playback;
  if (!playback.begin(self, ctx.outputPin, kOpenAiTtsSampleRate, ctx.volume) ||
      !playback.startDecoder(self, playback.radio)) {
    playback.end();
    vStreamBufferDelete(streamBuffer);
    finishPlaybackTask(self);
    return;
  }

  self->log(INFO, "🔊 Streaming WAV playback started (GPIO %d, volume %.2f)",
            ctx.outputPin, ctx.volume);

  WavChunkReader reader;
  reader.read = [streamBuffer](uint8_t* buffer, size_t maxLen) -> size_t {
    return xStreamBufferReceive(streamBuffer, buffer, maxLen,
                                pdMS_TO_TICKS(100));
  };
  reader.waitForMore = [&downloadDone, streamBuffer]() -> bool {
    if (downloadDone && xStreamBufferBytesAvailable(streamBuffer) == 0) {
      return false;
    }
    return true;
  };

  const bool playbackOk =
      pumpWavDecoder(self, *playback.decoder, pttPin, nullptr, reader,
                     kStreamReadChunkBytes);

  playback.end();
  vStreamBufferDelete(streamBuffer);

  if (!downloadState.ok || !playbackOk) {
    self->log(ERROR, "🔊 Streaming WAV playback failed");
  } else {
    self->log(INFO, "🔊 Streaming WAV playback finished");
  }

  finishPlaybackTask(self);
}

static void audioPlaybackTask(void* param) {
  AudioPlaybackContext ctx = *static_cast<AudioPlaybackContext*>(param);
  delete static_cast<AudioPlaybackContext*>(param);

  ESPWiFi* self = ctx.self;
  const int pttPin = ctx.pttPin;

  self->log(INFO, "🔊 Playing WAV file: %s", ctx.path.c_str());
  FILE* file = fopen(ctx.path.c_str(), "rb");
  if (file == nullptr) {
    self->log(ERROR, "🔊 Failed to open audio file: %s", ctx.path.c_str());
    finishPlaybackTask(self);
    return;
  }

  WAVAudioInfo wavInfo;
  const bool hasWavInfo = probeWavFormat(file, wavInfo);
  if (hasWavInfo) {
    self->log(INFO, "🔊 WAV format: %d Hz, %d ch, %d bit", wavInfo.sample_rate,
              wavInfo.channels, wavInfo.bits_per_sample);
  } else {
    self->log(WARNING, "🔊 Could not parse WAV format; using defaults");
  }

  const int sampleRate =
      hasWavInfo && wavInfo.sample_rate > 0 ? wavInfo.sample_rate : 44100;

  DacPlayback playback;
  if (!playback.begin(self, ctx.outputPin, sampleRate, ctx.volume)) {
    fclose(file);
    finishPlaybackTask(self);
    return;
  }

  StereoToMonoPrint stereoDownmix;
  Print* pipelineOut = &playback.radio;

  if (hasWavInfo && wavInfo.channels > 1) {
    stereoDownmix.setOutput(playback.radio);
    pipelineOut = &stereoDownmix;
    self->log(INFO, "🔊 Downmixing stereo WAV to mono for DAC GPIO %d",
              ctx.outputPin);
  }

  ResampleStream* resampler =
      maybeCreateResampler(self, *pipelineOut, wavInfo, sampleRate);
  Print& pcmSink =
      resampler != nullptr ? static_cast<Print&>(*resampler) : *pipelineOut;

  if (!playback.startDecoder(self, pcmSink)) {
    delete resampler;
    playback.end();
    fclose(file);
    finishPlaybackTask(self);
    return;
  }

  self->log(INFO, "🔊 DAC playback started: %s (GPIO %d, volume %.2f)",
            ctx.path.c_str(), ctx.outputPin, ctx.volume);

  WavChunkReader reader;
  reader.read = [file](uint8_t* buffer, size_t maxLen) -> size_t {
    return fread(buffer, 1, maxLen, file);
  };

  pumpWavDecoder(self, *playback.decoder, pttPin, resampler, reader,
                 kFileReadChunkBytes);

  playback.end();
  delete resampler;
  fclose(file);

  if (ctx.deleteAfterPlay) {
    if (remove(ctx.path.c_str()) == 0) {
      self->log(INFO, "🔊 Deleted audio file: %s", ctx.path.c_str());
    } else {
      self->log(WARNING, "🔊 Failed to delete audio file: %s", ctx.path.c_str());
    }
  }

  self->log(INFO, "🔊 DAC playback finished");
  finishPlaybackTask(self);
}

}  // namespace

void ESPWiFi::playAudio(const std::string& path, float volume, int outputPin,
                        bool deleteAfterPlay) {
  if (!isValidDacPin(outputPin)) {
    log(WARNING, "🔊 Invalid DAC pin %d (use GPIO %d or %d)", outputPin,
        ESPWiFi_DAC_PIN_1, ESPWiFi_DAC_PIN_2);
    return;
  }

  volume = clampVolume(volume);

  if (audioTask != nullptr) {
    stopAudioPlayback();
  }

  audioFilePath = resolveAudioFilePath(this, path);
  if (audioFilePath.empty()) {
    log(WARNING, "🔊 No audio file path provided");
    return;
  }

  FILE* probe = fopen(audioFilePath.c_str(), "rb");
  if (probe == nullptr) {
    log(ERROR, "🔊 Audio file not found: %s", audioFilePath.c_str());
    return;
  }
  fclose(probe);

  audioOutputPin = outputPin;
  audioPlaying = true;

  auto* ctx = new AudioPlaybackContext{this,     audioFilePath, volume,
                                       outputPin, audioPttPin,   deleteAfterPlay};
  if (xTaskCreatePinnedToCore(audioPlaybackTask, "dac-audio", 12288, ctx, 5,
                              &audioTask, 1) != pdPASS) {
    log(ERROR, "🔊 Failed to create DAC playback task");
    delete ctx;
    audioPlaying = false;
    audioTask = nullptr;
  }
}

void ESPWiFi::playStreamingWav(float volume, int outputPin,
                               AudioStreamProvider provider) {
  if (!isValidDacPin(outputPin)) {
    log(WARNING, "🔊 Invalid DAC pin %d (use GPIO %d or %d)", outputPin,
        ESPWiFi_DAC_PIN_1, ESPWiFi_DAC_PIN_2);
    return;
  }

  volume = clampVolume(volume);

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
  if (xTaskCreatePinnedToCore(streamingAudioPlaybackTask, "dac-audio-stream",
                              16384, ctx, 5, &audioTask, 1) != pdPASS) {
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
    setGPIO(audioPttPin, "low");
  }

  log(INFO, "🔊 DAC playback stopped");
}

#endif  // ESPWiFi_DAC_ENABLED
