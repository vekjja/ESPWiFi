// BluetoothAudio.cpp - Bluetooth Classic A2DP source (WAV-from-SD) using
// pschatzmann/ESP32-A2DP
//
// Why this approach:
// - Uses widely adopted, actively maintained A2DP wrapper:
//   `https://github.com/pschatzmann/ESP32-A2DP`
// - Keeps HTTP handlers fast: routes enqueue actions; BT/audio work happens in
//   a dedicated task.
// - A2DP data callback never blocks; underruns return silence.
//
// WAV-first constraints (strict, for reliability):
// - PCM (audioFormat=1), 16-bit little-endian
// - Stereo (2ch)
// - 44.1kHz
//
// MP3/OGG can be added later by decoding to PCM before pushing into the ring
// buffer.

#include "ESPWiFi.h"

#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_BLUEDROID_ENABLED) &&      \
    defined(CONFIG_BT_CLASSIC_ENABLED)

#include "BluetoothA2DPSource.h"
#include "esp_a2dp_api.h"
#include "esp_gap_bt_api.h"
#include "esp_heap_caps.h"
#include "freertos/stream_buffer.h"

#include <cstring>

namespace {

// Keep this small: BT + WiFi + httpd can leave the heap fragmented/tight.
// Prefer static buffering to avoid heap fragmentation. This is the size of the
// PCM buffer used for feeding the A2DP callback.
constexpr size_t kPcmBytes = 8 * 1024;
// Keep the heap ringbuffer for now (legacy path); will be replaced by the
// static stream buffer below.
constexpr size_t kRingBytes = 16 * 1024;
// Keep this small: each command carries a small payload; big queues just waste
// RAM.
constexpr size_t kCmdQDepth = 4;
constexpr size_t kMaxScanResults = 16;

static ESPWiFi *s_self = nullptr;
// Small wrapper so we can stop the library's scan-restart loop without
// tearing down the whole BT stack (i.e., without calling end()).
class ESPWiFiA2DPSource : public BluetoothA2DPSource {
public:
  void setEndFlag(bool v) { is_end = v; }
  bool discoveryActive() const { return discovery_active; }
  void setStreamingEnabled(bool en) { streamingEnabled_ = en; }

protected:
  // The upstream library starts media automatically on heartbeats after
  // connect. On low-heap boards this can trigger repeated ~4KB allocations even
  // when we are not actually playing anything. Gate media start/processing
  // behind an explicit "play is active" flag.
  void bt_app_av_media_proc(uint16_t event, void *param) override {
    if (!streamingEnabled_) {
      (void)event;
      (void)param;
      return;
    }
    BluetoothA2DPSource::bt_app_av_media_proc(event, param);
  }

private:
  bool streamingEnabled_ = false;
};

static ESPWiFiA2DPSource s_a2dp;

static TaskHandle_t s_btTask = nullptr;
static QueueHandle_t s_cmdQ = nullptr;
// Use a static StreamBuffer for PCM to avoid heap allocation failures when the
// heap is fragmented (e.g. largest_free_block < 16KB).
static StreamBufferHandle_t s_pcmSb = nullptr;
static StaticStreamBuffer_t s_pcmSbStruct{};
static uint8_t s_pcmSbStorage[kPcmBytes]{};

static bool s_started = false;
static bool s_playRequested = false;
static bool s_stopPlayback = false;
static bool s_streamingEnabled = false;
static char s_targetName[64]{};
static char s_playPath[192]{};

struct ScanResult {
  esp_bd_addr_t addr{};
  int rssi = -129;
  char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1]{};
  bool inUse = false;
};

static ScanResult s_scan[kMaxScanResults]{};
static bool s_scanActive = false;
static int64_t s_scanStopAtUs = 0;
static bool s_connectFromScan = false;
static bool s_btStartedOnce = false;

// Avoid large stack allocations in the bt_audio task.
static uint8_t s_wavIoBuf[4096];

enum class BtCmdType : uint8_t {
  EnsureInit = 1,
  StartPairing = 2, // repurposed: discovery window (scan)
  ConnectByName = 3,
  Disconnect = 4,
  PlayWav = 5,
  Stop = 6,
};

struct BtCmd {
  BtCmdType type;
  uint32_t u32 = 0;
  char str[192]{};
};

static void clearScan_() {
  for (auto &r : s_scan) {
    r.inUse = false;
    r.rssi = -129;
    r.name[0] = '\0';
    memset(r.addr, 0, sizeof(r.addr));
  }
}

static bool ssidFoundCb_(const char *ssid, esp_bd_addr_t address, int rrsi) {
  // Called from BT stack. Keep it tiny, no allocations, no blocking.
  // Always record results.
  int slot = -1;
  for (int i = 0; i < (int)kMaxScanResults; i++) {
    if (s_scan[i].inUse &&
        memcmp(s_scan[i].addr, address, ESP_BD_ADDR_LEN) == 0) {
      slot = i;
      break;
    }
    if (!s_scan[i].inUse && slot < 0) {
      slot = i;
    }
  }
  if (slot >= 0) {
    s_scan[slot].inUse = true;
    memcpy(s_scan[slot].addr, address, ESP_BD_ADDR_LEN);
    s_scan[slot].rssi = rrsi;
    if (ssid && ssid[0] != '\0') {
      strncpy(s_scan[slot].name, ssid, sizeof(s_scan[slot].name) - 1);
      s_scan[slot].name[sizeof(s_scan[slot].name) - 1] = '\0';
    }
  }

  // Scan mode: never select a target (prevents auto-connect).
  // Note: discovery_active == true during BOTH scan and connect attempts, so we
  // must key off our explicit mode flag instead of s_scanActive.
  if (!s_connectFromScan) {
    return false;
  }

  // Connect mode: select device if it matches our configured target prefix.
  if (s_targetName[0] == '\0' || !ssid) {
    return false;
  }
  const size_t wantLen = strlen(s_targetName);
  if (wantLen == 0) {
    return false;
  }
  const bool match = strncmp(ssid, s_targetName, wantLen) == 0;
  if (match && s_self) {
    s_self->log(INFO, "🔎 BT match: \"%s\" (rssi=%d) -> selecting target", ssid,
                rrsi);
  }
  return match;
}

static void discoveryModeCb_(esp_bt_gap_discovery_state_t st) {
  s_scanActive = (st == ESP_BT_GAP_DISCOVERY_STARTED);
}

// ---- WAV parsing (strict)
// ----------------------------------------------------
struct WavInfo {
  uint16_t audioFormat = 0;   // 1 = PCM
  uint16_t numChannels = 0;   // 2
  uint32_t sampleRate = 0;    // 44100
  uint16_t bitsPerSample = 0; // 16
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
};

static bool readExact_(FILE *f, void *dst, size_t n) {
  return f && dst && fread(dst, 1, n, f) == n;
}

static bool parseWav_(FILE *f, WavInfo &out) {
  if (!f)
    return false;
  memset(&out, 0, sizeof(out));

  char riff[4];
  uint32_t riffSize = 0;
  char wave[4];
  if (!readExact_(f, riff, 4) || !readExact_(f, &riffSize, 4) ||
      !readExact_(f, wave, 4)) {
    return false;
  }
  (void)riffSize;
  if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
    return false;
  }

  bool foundFmt = false;
  bool foundData = false;
  while (!foundData) {
    char chunkId[4];
    uint32_t chunkSize = 0;
    if (!readExact_(f, chunkId, 4) || !readExact_(f, &chunkSize, 4)) {
      return false;
    }
    long chunkDataPos = ftell(f);
    if (chunkDataPos < 0)
      return false;

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      if (chunkSize < 16)
        return false;
      uint16_t audioFormat = 0;
      uint16_t numChannels = 0;
      uint32_t sampleRate = 0;
      uint32_t byteRate = 0;
      uint16_t blockAlign = 0;
      uint16_t bitsPerSample = 0;
      if (!readExact_(f, &audioFormat, 2) || !readExact_(f, &numChannels, 2) ||
          !readExact_(f, &sampleRate, 4) || !readExact_(f, &byteRate, 4) ||
          !readExact_(f, &blockAlign, 2) || !readExact_(f, &bitsPerSample, 2)) {
        return false;
      }
      (void)byteRate;
      (void)blockAlign;
      out.audioFormat = audioFormat;
      out.numChannels = numChannels;
      out.sampleRate = sampleRate;
      out.bitsPerSample = bitsPerSample;
      foundFmt = true;
    } else if (memcmp(chunkId, "data", 4) == 0) {
      out.dataOffset = static_cast<uint32_t>(chunkDataPos);
      out.dataSize = chunkSize;
      foundData = true;
    }

    uint32_t skip = chunkSize;
    if (skip & 1)
      skip++;
    if (fseek(f, chunkDataPos + static_cast<long>(skip), SEEK_SET) != 0) {
      return false;
    }
    if (foundData && !foundFmt) {
      return false;
    }
  }

  if (out.audioFormat != 1 || out.numChannels != 2 || out.sampleRate != 44100 ||
      out.bitsPerSample != 16 || out.dataSize == 0) {
    return false;
  }
  return true;
}

// ---- PCM buffer helpers
// ------------------------------------------------------
static void rbReset_() {
  if (!s_pcmSb)
    return;
  (void)xStreamBufferReset(s_pcmSb);
}

// ---- A2DP data callback (must not block) ------------------------------------
static int32_t a2dpDataCb_(uint8_t *data, int32_t byteCount) {
  if (!data || byteCount <= 0) {
    return 0;
  }
  memset(data, 0, (size_t)byteCount);
  if (!s_pcmSb) {
    return byteCount;
  }

  // Non-blocking read; any underrun stays as zeros.
  (void)xStreamBufferReceive(s_pcmSb, data, (size_t)byteCount, 0);
  return byteCount;
}

static bool ensurePcmBuf_() {
  if (s_pcmSb) {
    return true;
  }
  if (!s_self) {
    return false;
  }

  const size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  s_self->log(DEBUG, "� BT heap: free=%u largest=%u pcm_buf=%u",
              (unsigned)free8, (unsigned)largest8, (unsigned)kPcmBytes);

  // Static stream buffer: no heap allocation, immune to fragmentation.
  s_pcmSb =
      xStreamBufferCreateStatic(kPcmBytes, 1, s_pcmSbStorage, &s_pcmSbStruct);
  if (!s_pcmSb) {
    s_self->log(ERROR, "🛜 BT audio: PCM streambuffer create failed");
    return false;
  }
  return true;
}

// ---- init/start
// --------------------------------------------------------------
static bool ensureInit_() {
  if (s_started) {
    return true;
  }
  if (!s_self) {
    return false;
  }

  // Library connects to the *speaker* by its name.
  // Mirror the library example behavior: don't auto-reconnect unless explicitly
  // requested (keeps behavior predictable).
  s_a2dp.set_auto_reconnect(false);
  s_a2dp.set_data_callback(a2dpDataCb_);
  // Enable discovery callbacks so we can expose scan results.
  s_a2dp.set_ssid_callback(ssidFoundCb_);
  s_a2dp.set_discovery_mode_callback(discoveryModeCb_);

  // Not started until we have a target name.
  s_started = true;
  s_self->log(INFO, "🛜 BT audio ready (ESP32-A2DP)");
  return true;
}

static void logBtHeapHint_(const char *where) {
  if (!s_self)
    return;
  const size_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const size_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  s_self->log(INFO, "🛜 BT heap (%s): free=%u largest=%u", where,
              (unsigned)free8, (unsigned)largest8);
  if (largest8 < 8192) {
    s_self->log(WARNING, "🛜 BT heap fragmented/low (largest<8KB). A2DP may "
                         "fail allocating TX buffers.");
  }
}

static void connectByName_(const char *name) {
  if (!ensureInit_())
    return;
  if (!name || name[0] == '\0') {
    s_self->log(WARNING, "🛜 BT connect: missing speaker name");
    return;
  }
  strncpy(s_targetName, name, sizeof(s_targetName) - 1);
  s_targetName[sizeof(s_targetName) - 1] = '\0';

  // Use the library's discovery + ssid_callback selection path.
  // This also collects scan results while connecting.
  s_connectFromScan = true;
  // Don't start streaming until "Play" is active.
  s_streamingEnabled = false;
  s_a2dp.setStreamingEnabled(false);
  s_a2dp.setEndFlag(false);
  s_scanActive = false;
  logBtHeapHint_("before connect");
  s_self->log(INFO, "🛜 BT connect (discover): %s", s_targetName);
  s_a2dp.start(); // no name: selection happens via ssid callback
  s_btStartedOnce = true;
}

static void disconnect_() {
  // Stop streaming + disconnect (keeps memory by default).
  s_connectFromScan = false;
  s_a2dp.disconnect();
  s_self->log(INFO, "🛜 BT stop/disconnect requested");
}

static void playWav_(const char *path) {
  if (!ensureInit_())
    return;
  if (!path || path[0] == '\0') {
    s_self->log(WARNING, "🛜 BT play: missing path");
    return;
  }
  if (strncmp(path, "/sd/", 4) != 0) {
    s_self->log(WARNING, "🛜 BT play: path must start with /sd/: %s", path);
    return;
  }
  strncpy(s_playPath, path, sizeof(s_playPath) - 1);
  s_playPath[sizeof(s_playPath) - 1] = '\0';
  s_playRequested = true;
  s_stopPlayback = false;

  if (!ensurePcmBuf_()) {
    // Can't play without a PCM buffer (we'd just output silence).
    s_playRequested = false;
    s_stopPlayback = false;
    return;
  }

  rbReset_();

  // Enable A2DP media only while playing.
  s_streamingEnabled = true;
  s_a2dp.setStreamingEnabled(true);
  if (s_a2dp.is_connected()) {
    (void)esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
  }

  // If a target name is configured but we aren't connected yet, kick off a
  // single connection attempt here (do NOT spam start() in the playback loop).
  if (s_targetName[0] != '\0' && !s_a2dp.is_connected()) {
    s_self->log(INFO, "🛜 BT connecting (auto from play): %s", s_targetName);
    s_a2dp.start(s_targetName);
  }

  s_self->log(INFO, "🎵 BT play requested: %s", s_playPath);
}

static void stop_() {
  s_stopPlayback = true;
  s_playRequested = false;
  rbReset_();
  s_connectFromScan = false;
  s_streamingEnabled = false;
  s_a2dp.setStreamingEnabled(false);
  // Best-effort stop of A2DP media to avoid BT TX allocations when idle.
  (void)esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_STOP);
  s_self->log(INFO, "🛑 BT stop requested");
}

static bool enqueue_(const BtCmd &cmd) {
  return s_cmdQ && xQueueSend(s_cmdQ, &cmd, 0) == pdTRUE;
}

static void btTask_(void *) {
  BtCmd cmd{};

  for (;;) {
    // Stop scanning after requested window.
    if (s_scanActive && s_scanStopAtUs > 0 &&
        esp_timer_get_time() >= s_scanStopAtUs) {
      // Stop discovery loop WITHOUT end(): calling end() deinitializes stacks
      // and causes heap churn + warnings on repeated scans.
      s_self->log(INFO, "🔎 BT scan finished");
      s_a2dp.setEndFlag(true);             // prevents restart in library
      (void)esp_bt_gap_cancel_discovery(); // async; library callback will flip
                                           // discovery_active
      s_scanActive = false;
      s_scanStopAtUs = 0;
    }

    // Feed ringbuffer when playing (kept in this task)
    if (s_playRequested && !s_stopPlayback && s_playPath[0] != '\0') {
      FILE *f = fopen(s_playPath, "rb");
      if (!f) {
        s_self->log(ERROR, "💔 BT play: fopen failed: %s", s_playPath);
        s_playRequested = false;
      } else {
        WavInfo wi{};
        const bool wavOk = parseWav_(f, wi);
        if (!wavOk || fseek(f, (long)wi.dataOffset, SEEK_SET) != 0) {
          s_self->log(
              ERROR,
              "💔 BT play: unsupported WAV (need PCM16 stereo 44.1k): %s",
              s_playPath);
          s_self->log(ERROR,
                      "💔 WAV details: fmt=%u ch=%u hz=%u bps=%u data=%u",
                      (unsigned)wi.audioFormat, (unsigned)wi.numChannels,
                      (unsigned)wi.sampleRate, (unsigned)wi.bitsPerSample,
                      (unsigned)wi.dataSize);
          fclose(f);
          s_playRequested = false;
        } else {
          s_self->log(INFO, "🎶 WAV OK: PCM16 stereo 44.1k (%u bytes)",
                      (unsigned)wi.dataSize);
          uint32_t remaining = wi.dataSize;

          while (remaining > 0 && s_playRequested && !s_stopPlayback) {
            const size_t toRead = (remaining > sizeof(s_wavIoBuf))
                                      ? sizeof(s_wavIoBuf)
                                      : (size_t)remaining;
            size_t n = fread(s_wavIoBuf, 1, toRead, f);
            if (n == 0) {
              break;
            }
            remaining -= (uint32_t)n;

            if (s_pcmSb) {
              if (xStreamBufferSend(s_pcmSb, s_wavIoBuf, n,
                                    pdMS_TO_TICKS(50)) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
              }
            }
            vTaskDelay(1);
          }
          fclose(f);
          s_self->log(INFO, "🎵 WAV stream finished");
          s_playRequested = false;
          s_stopPlayback = false;
          rbReset_();
        }
      }
    }

    if (xQueueReceive(s_cmdQ, &cmd, pdMS_TO_TICKS(25)) == pdTRUE) {
      switch (cmd.type) {
      case BtCmdType::EnsureInit:
        (void)ensureInit_();
        break;
      case BtCmdType::StartPairing: {
        // Discovery window: collect nearby compatible audio devices.
        if (!ensureInit_()) {
          break;
        }
        clearScan_();
        s_connectFromScan = false;
        // Scan-only mode: prevent the library from re-starting discovery when
        // an inquiry naturally ends (it retries when is_end == false).
        s_a2dp.setEndFlag(true);
        const uint32_t sec = (cmd.u32 == 0 ? 10 : cmd.u32);
        s_scanStopAtUs = esp_timer_get_time() + (int64_t)sec * 1000000LL;
        s_self->log(INFO, "🔎 BT scan started (%us)", (unsigned)cmd.u32);
        // First scan brings up the BT stack via the library.
        if (!s_btStartedOnce) {
          s_a2dp.start();
          s_btStartedOnce = true;
        } else {
          // Subsequent scans: just start inquiry (library's GAP callback will
          // collect results via ssid_callback).
          (void)esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, sec,
                                           0);
        }
        break;
      }
      case BtCmdType::ConnectByName:
        connectByName_(cmd.str);
        break;
      case BtCmdType::Disconnect:
        disconnect_();
        break;
      case BtCmdType::PlayWav:
        playWav_(cmd.str);
        break;
      case BtCmdType::Stop:
        stop_();
        break;
      default:
        break;
      }
    }
  }
}

} // namespace

// -----------------------------------------------------------------------------
// ESPWiFi public methods
// -----------------------------------------------------------------------------
bool ESPWiFi::startBluetoothAudio() {
  s_self = this;

  if (!s_cmdQ) {
    s_cmdQ = xQueueCreate(kCmdQDepth, sizeof(BtCmd));
    if (!s_cmdQ) {
      log(ERROR, "💔 BT audio: cmd queue alloc failed");
      return false;
    }
  }

  if (!s_btTask) {
    // Note: FreeRTOS stack size is in words (not bytes).
    // Keep this reasonable to preserve heap; we already keep large buffers
    // off-stack except for the 4KB WAV read chunk.
    // Stack size is in words. After moving large buffers to static storage, we
    // can keep this smaller to preserve heap.
    xTaskCreate(btTask_, "bt_audio", 6144, nullptr, 5, &s_btTask);
  }

  BtCmd cmd{};
  cmd.type = BtCmdType::EnsureInit;
  (void)enqueue_(cmd);
  return true;
}

void ESPWiFi::bluetoothAudioConfigHandler() {
#if defined(CONFIG_BT_ENABLED) && defined(CONFIG_BT_BLUEDROID_ENABLED) &&      \
    defined(CONFIG_BT_CLASSIC_ENABLED)
  config["bluetooth"]["installed"] = true;

  const bool btEnabled = config["bluetooth"]["enabled"].isNull()
                             ? false
                             : config["bluetooth"]["enabled"].as<bool>();
  const bool audioEnabled =
      config["bluetooth"]["audio"]["enabled"].isNull()
          ? false
          : config["bluetooth"]["audio"]["enabled"].as<bool>();

  if (btEnabled && audioEnabled) {
    (void)startBluetoothAudio();
  } else {
    stopBluetoothAudio();
  }
#else
  config["bluetooth"]["installed"] = false;
  config["bluetooth"]["enabled"] = false;
  config["bluetooth"]["audio"]["enabled"] = false;
#endif
}

void ESPWiFi::stopBluetoothAudio() {
  BtCmd cmd{};
  cmd.type = BtCmdType::Stop;
  (void)enqueue_(cmd);
}

void ESPWiFi::btEnterPairingMode(uint32_t seconds) {
  BtCmd cmd{};
  cmd.type = BtCmdType::StartPairing;
  cmd.u32 = (seconds == 0) ? 10 : (seconds > 60 ? 60 : seconds);
  (void)enqueue_(cmd);
}

void ESPWiFi::btStopPairingMode() {
  // Best-effort: stop connection attempts/streaming
  BtCmd cmd{};
  cmd.type = BtCmdType::Disconnect;
  (void)enqueue_(cmd);
}

void ESPWiFi::btConnect(const std::string &addrOrName) {
  // With ESP32-A2DP we connect by *speaker name*.
  BtCmd cmd{};
  cmd.type = BtCmdType::ConnectByName;
  strncpy(cmd.str, addrOrName.c_str(), sizeof(cmd.str) - 1);
  cmd.str[sizeof(cmd.str) - 1] = '\0';
  (void)enqueue_(cmd);
}

void ESPWiFi::btDisconnect() {
  BtCmd cmd{};
  cmd.type = BtCmdType::Disconnect;
  (void)enqueue_(cmd);
}

void ESPWiFi::btPlayWavFromSd(const std::string &path) {
  BtCmd cmd{};
  cmd.type = BtCmdType::PlayWav;
  strncpy(cmd.str, path.c_str(), sizeof(cmd.str) - 1);
  cmd.str[sizeof(cmd.str) - 1] = '\0';
  (void)enqueue_(cmd);
}

void ESPWiFi::btStopAudio() {
  BtCmd cmd{};
  cmd.type = BtCmdType::Stop;
  (void)enqueue_(cmd);
}

std::string ESPWiFi::btStatusJson() {
  JsonDocument doc;
  doc["installed"] = true;
  doc["enabled"] = config["bluetooth"]["enabled"].isNull()
                       ? false
                       : config["bluetooth"]["enabled"].as<bool>();
  doc["audioEnabled"] =
      config["bluetooth"]["audio"]["enabled"].isNull()
          ? false
          : config["bluetooth"]["audio"]["enabled"].as<bool>();
  doc["started"] = s_started;
  doc["playing"] = s_playRequested;
  doc["targetName"] = s_targetName;
  doc["path"] = s_playPath;
  doc["connected"] = s_a2dp.is_connected();
  doc["connecting"] =
      (!doc["connected"].as<bool>() && (s_targetName[0] != '\0'));

  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string ESPWiFi::btScanJson() {
  JsonDocument doc;
  doc["scanning"] = s_scanActive;
  JsonArray arr = doc["devices"].to<JsonArray>();
  for (const auto &r : s_scan) {
    if (!r.inUse)
      continue;
    JsonObject o = arr.add<JsonObject>();
    char addr[24];
    snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X", r.addr[0],
             r.addr[1], r.addr[2], r.addr[3], r.addr[4], r.addr[5]);
    o["addr"] = addr;
    o["name"] = r.name;
    o["rssi"] = r.rssi;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

#else

bool ESPWiFi::startBluetoothAudio() {
  log(INFO, "📡 Bluetooth Classic not enabled in sdkconfig");
  return false;
}
void ESPWiFi::stopBluetoothAudio() {}
void ESPWiFi::btEnterPairingMode(uint32_t) {}
void ESPWiFi::btStopPairingMode() {}
void ESPWiFi::btConnect(const std::string &) {}
void ESPWiFi::btDisconnect() {}
void ESPWiFi::btPlayWavFromSd(const std::string &) {}
void ESPWiFi::btStopAudio() {}
std::string ESPWiFi::btStatusJson() { return "{\"installed\":false}"; }
std::string ESPWiFi::btScanJson() { return "{\"devices\":[]}"; }

#endif
