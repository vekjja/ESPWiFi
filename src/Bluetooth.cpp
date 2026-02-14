
#ifndef ESPWiFi_BLUETOOTH_H
#define ESPWiFi_BLUETOOTH_H

#include "ESPWiFi.h"
#if defined(CONFIG_BT_A2DP_ENABLE)

#include <cstring>

#define MINIMP3_IMPLEMENTATION
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "BluetoothA2DPSource.h"

using namespace audio_tools;

static ESPWiFi* bt_espwifi = nullptr;
static BluetoothA2DPSource s_a2dp_source;

static int32_t silentDataCb(uint8_t* data, int32_t len) {
  if (data && len > 0) {
    memset(data, 0, (size_t)len);
    return len;
  }
  return 0;
}

static bool onSsidDiscovered(const char* ssid, esp_bd_addr_t address,
                             int rssi) {
  if (!bt_espwifi || !ssid) return false;
  if (!bt_espwifi->bluetoothConnectTargetName.empty() &&
      bt_espwifi->bluetoothConnectTargetName == ssid) {
    bt_espwifi->bluetoothConnectTargetName.clear();
    return true;
  }
  bool alreadyInList = false;
  for (const auto& s : bt_espwifi->bluetoothScannedHosts)
    if (s == ssid) {
      alreadyInList = true;
      break;
    }
  if (!alreadyInList) {
    bt_espwifi->bluetoothScannedHosts.push_back(ssid);
  }
  if (bt_espwifi->onBluetoothDeviceDiscovered)
    return bt_espwifi->onBluetoothDeviceDiscovered(ssid, address, rssi);
  return false;
}

static void onConnectionStateChanged(esp_a2d_connection_state_t state,
                                     void* obj) {
  (void)obj;
  if (bt_espwifi->onBluetoothConnectionStateChanged)
    bt_espwifi->onBluetoothConnectionStateChanged(state);
}

void ESPWiFi::startBluetooth() {
  if (a2dp_source != nullptr) {
    log(INFO, "🛜 Bluetooth already started");
    return;
  }
  bt_espwifi = this;
  s_a2dp_source.set_data_callback(silentDataCb);
  s_a2dp_source.set_ssid_callback(onSsidDiscovered);
  s_a2dp_source.set_on_connection_state_changed(onConnectionStateChanged);
  s_a2dp_source.start(getHostname().c_str());
  a2dp_source = &s_a2dp_source;
  log(INFO, "🛜 Bluetooth Started");
}

void ESPWiFi::stopBluetooth() {
  if (a2dp_source == nullptr) return;
  s_a2dp_source.end();
  a2dp_source = nullptr;
  bt_espwifi = nullptr;
  log(INFO, "🛜 Bluetooth Stopped");
}

void ESPWiFi::toggleBluetooth() {
  if (a2dp_source == nullptr) {
    startBluetooth();
  } else {
    stopBluetooth();
  }
}

void ESPWiFi::startBluetoothMp3Playback(const char* path) {
  (void)path;
  log(WARNING, "🛜🎵 MP3 playback not implemented");
}

// ---------------------------------------------------------------------------
// WAV → A2DP streaming  (using arduino-audio-tools WAVDecoder)
//
// Architecture:
//   1.  A FreeRTOS task reads the WAV file in chunks and feeds them to
//       WAVDecoder::write().  The decoder parses the RIFF/WAVE header,
//       locates the data chunk, and converts 8/24/32-bit PCM to 16-bit.
//   2.  Decoded 16-bit PCM flows through a RingBufferPrint into a SPSC
//       ring buffer.
//   3.  The A2DP data callback pulls PCM from the ring buffer; silence
//       is returned when the buffer is empty to keep the link alive.
// ---------------------------------------------------------------------------

static constexpr size_t kWavRingBufSize = 8192;
static uint8_t wav_ring_storage[kWavRingBufSize];
static volatile size_t wav_ring_head = 0;
static volatile size_t wav_ring_tail = 0;

static size_t wavRingAvailable() {
  size_t h = wav_ring_head, t = wav_ring_tail;
  return (h >= t) ? (h - t) : (kWavRingBufSize - t + h);
}
static size_t wavRingFree() { return kWavRingBufSize - 1 - wavRingAvailable(); }

static size_t wavRingWrite(const uint8_t* data, size_t len) {
  size_t free = wavRingFree();
  if (len > free) len = free;
  for (size_t i = 0; i < len; i++) {
    wav_ring_storage[wav_ring_head] = data[i];
    wav_ring_head = (wav_ring_head + 1) % kWavRingBufSize;
  }
  return len;
}
static size_t wavRingRead(uint8_t* data, size_t len) {
  size_t avail = wavRingAvailable();
  if (len > avail) len = avail;
  for (size_t i = 0; i < len; i++) {
    data[i] = wav_ring_storage[wav_ring_tail];
    wav_ring_tail = (wav_ring_tail + 1) % kWavRingBufSize;
  }
  return len;
}
static void wavRingReset() { wav_ring_head = 0; wav_ring_tail = 0; }

// A2DP data callback for WAV playback.
static int32_t wavDataCb(uint8_t* data, int32_t len) {
  if (len <= 0) return 0;
  size_t got = wavRingRead(data, (size_t)len);
  if (got < (size_t)len) memset(data + got, 0, (size_t)len - got);
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
      size_t chunk = wavRingWrite(data + written, len - written);
      written += chunk;
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
  while (wavRingAvailable() > 0 && drainMs < 2000) {
    vTaskDelay(pdMS_TO_TICKS(20));
    drainMs += 20;
  }

  s_a2dp_source.set_data_callback(silentDataCb);
  wavRingReset();

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
  wavRingReset();

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
  wavRingReset();
  log(INFO, "🛜🎵 WAV playback stopped");
}

void ESPWiFi::stopBluetoothMp3Playback() {}

#endif  // CONFIG_BT_A2DP_ENABLE
#endif  // ESPWiFi_BLUETOOTH_H
