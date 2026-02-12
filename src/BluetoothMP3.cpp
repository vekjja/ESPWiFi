#include "ESPWiFi.h"

#if defined(ESPWiFi_BT_ENABLED) && defined(CONFIG_BT_A2DP_ENABLE)

#include "BluetoothA2DPSource.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include <cstring>
#include <stdio.h>

#define MINIMP3_IMPLEMENTATION
#include "AudioTools/AudioCodecs/AudioEncoded.h"
#include "AudioTools/AudioCodecs/CodecMP3Mini.h"
#include "AudioTools/CoreAudio.h"

static ESPWiFi *s_bt_espwifi = nullptr;
static BluetoothA2DPSource *s_a2dp = nullptr;

#define PCM_STREAM_BUF_SIZE (2048)   // smaller to leave heap for A2DP malloc(4112)
#define PCM_STREAM_TRIGGER (256)
static StreamBufferHandle_t s_pcm_stream = nullptr;
static TaskHandle_t s_decoder_task = nullptr;
static volatile bool s_decoder_run = false;

static int32_t silentDataCb(uint8_t *data, int32_t len) {
  if (data && len > 0) {
    memset(data, 0, (size_t)len);
    return len;
  }
  return 0;
}

/// Print implementation that writes decoded PCM into the StreamBuffer for the
/// A2DP callback.
class PCMBufferWriter : public audio_tools::Print {
public:
  void setBuffer(StreamBufferHandle_t buf) { stream_ = buf; }
  size_t write(uint8_t ch) override {
    return stream_ ? (xStreamBufferSend(stream_, &ch, 1, 0) == 1 ? 1u : 0u) : 0;
  }
  size_t write(const uint8_t *data, size_t len) override {
    if (!stream_ || !data)
      return 0;
    size_t sent = 0;
    while (sent < len) {
      size_t n = xStreamBufferSend(stream_, data + sent, len - sent,
                                   pdMS_TO_TICKS(100));
      if (n == 0)
        break;
      sent += n;
    }
    return sent;
  }

private:
  StreamBufferHandle_t stream_ = nullptr;
};

/// Stream that reads from a FILE* (like the Arduino File in
/// streams-sd_mp3-i2s).
class FileStream : public audio_tools::Stream {
public:
  void setFile(FILE *f) { f_ = f; }
  size_t readBytes(uint8_t *data, size_t len) override {
    if (!f_ || !data)
      return 0;
    return fread(data, 1, len, f_);
  }
  int available() override {
    if (!f_)
      return 0;
    long pos = ftell(f_);
    if (pos < 0)
      return 0;
    if (fseek(f_, 0, SEEK_END) != 0)
      return 0;
    long size = ftell(f_);
    fseek(f_, pos, SEEK_SET);
    return (int)(size - pos);
  }

private:
  FILE *f_ = nullptr;
};

static PCMBufferWriter s_pcm_writer;
static FileStream s_file_stream;
static FILE *s_mp3_file = nullptr;

static int32_t mp3DataCb(uint8_t *data, int32_t len) {
  if (!data || len <= 0) {
    return 0;
  }
  size_t need = (size_t)len;
  size_t copied = 0;
  if (s_pcm_stream) {
    copied = xStreamBufferReceive(s_pcm_stream, data, need, 0);
  }
  if (copied < need) {
    memset(data + copied, 0, need - copied);
  }
  return (int32_t)len;
}

static void mp3DecoderTask(void *pv) {
  s_pcm_writer.setBuffer(s_pcm_stream);

  static audio_tools::MP3DecoderMini decoder;
  decoder.setBufferLength(6 * 1024);  // balance assert safety vs heap for A2DP
  audio_tools::EncodedAudioStream decoded(&s_pcm_writer, &decoder);
  if (!decoded.begin()) {
    s_decoder_task = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  audio_tools::StreamCopy copier(decoded, s_file_stream, 512);

  while (s_decoder_run && copier.copy() > 0) {
    if (s_pcm_stream) {
      size_t free_space = xStreamBufferSpacesAvailable(s_pcm_stream);
      if (free_space < 256)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
  }

  decoded.end();
  s_decoder_task = nullptr;
  vTaskDelete(nullptr);
}

void ESPWiFi::startBluetoothMp3Playback(const char *path) {
  if (a2dp_source == nullptr) {
    startBluetooth();
  }
  if (a2dp_source == nullptr) {
    log(WARNING, "🛜🎵 Bluetooth not available");
    return;
  }
  stopBluetoothMp3Playback();

  s_bt_espwifi = this;
  s_a2dp = a2dp_source;

  s_mp3_file = fopen(path, "rb");
  if (!s_mp3_file) {
    log(WARNING, "🛜🎵 Cannot open MP3: %s", path);
    return;
  }

  s_pcm_stream = xStreamBufferCreate(PCM_STREAM_BUF_SIZE, PCM_STREAM_TRIGGER);
  if (!s_pcm_stream) {
    fclose(s_mp3_file);
    s_mp3_file = nullptr;
    log(WARNING, "🛜🎵 No memory for PCM stream");
    return;
  }

  s_file_stream.setFile(s_mp3_file);
  s_decoder_run = true;
  if (xTaskCreate(mp3DecoderTask, "mp3dec", 16384, nullptr, 5,
                  &s_decoder_task) != pdPASS) {
    s_decoder_run = false;
    vStreamBufferDelete(s_pcm_stream);
    s_pcm_stream = nullptr;
    fclose(s_mp3_file);
    s_mp3_file = nullptr;
    log(WARNING, "🛜🎵 Cannot create decoder task");
    return;
  }

  a2dp_source->set_data_callback(mp3DataCb);
  log(INFO, "🛜🎵 Playing MP3: %s", path);
}

void ESPWiFi::stopBluetoothMp3Playback() {
  s_decoder_run = false;
  while (s_decoder_task != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  s_file_stream.setFile(nullptr);
  if (s_mp3_file) {
    fclose(s_mp3_file);
    s_mp3_file = nullptr;
  }
  if (s_pcm_stream) {
    vStreamBufferDelete(s_pcm_stream);
    s_pcm_stream = nullptr;
  }
  if (a2dp_source) {
    a2dp_source->set_data_callback(silentDataCb);
  }
  s_a2dp = nullptr;
}

#endif
