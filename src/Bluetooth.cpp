
#include "ESPWiFi.h"
#if defined(ESPWiFi_BT_ENABLED) && defined(CONFIG_BT_A2DP_ENABLE)

#ifndef ESPWiFi_BLUETOOTH_H
#define ESPWiFi_BLUETOOTH_H

#include "BluetoothA2DPSource.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctype.h>

static const char *BT_TAG = "ESPWiFi_BT";

/** Scan duration in seconds (ESP-IDF inquiry allows up to 12). */
static constexpr int kBTScanDurationSec = 3;

struct BTHost {
  std::string name;
  std::string address;
  int rssi = 0;
  void connect(ESPWiFi *espwifi) const;
};

BluetoothA2DPSource *a2dp_source = nullptr;
static std::vector<BTHost> s_scan_results;
static std::vector<BTHost> s_btDiscoveredDevices;
static SemaphoreHandle_t s_scan_mutex = nullptr;
static TaskHandle_t s_scan_task_handle = nullptr;
static volatile bool s_scan_complete_flag = false;

static std::string bd_addr_to_string(const uint8_t *addr) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1],
           addr[2], addr[3], addr[4], addr[5]);
  return std::string(buf);
}

static bool bt_scan_collector(const char *ssid, esp_bd_addr_t address,
                              int rssi) {
  if (s_scan_mutex)
    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
  std::string addr = bd_addr_to_string(address);
  auto it =
      std::find_if(s_scan_results.begin(), s_scan_results.end(),
                   [&addr](const BTHost &h) { return h.address == addr; });
  if (it != s_scan_results.end()) {
    it->rssi = rssi;
    if (ssid && ssid[0])
      it->name = ssid;
  } else {
    BTHost host;
    host.name = ssid ? ssid : "";
    host.address = std::move(addr);
    host.rssi = rssi;
    s_scan_results.push_back(std::move(host));
  }
  if (s_scan_mutex)
    xSemaphoreGive(s_scan_mutex);
  return false;
}

/** When true, get_data_frames streams PCM; otherwise silence. */
static volatile bool s_bt_stream_playing = false;

/** Path to the MP3 file to play when Play is pressed (on SD card). */
static const char *const kBTStreamMp3Path = "/sd/music/we r.mp3";

/* Simple ring buffer for PCM samples. Decode task writes, callback reads. */
#define PCM_RING_SIZE 4096
static int16_t s_pcm_ring[PCM_RING_SIZE];
static volatile int s_pcm_ring_read = 0;
static volatile int s_pcm_ring_write = 0;
static FILE *s_mp3_file = nullptr;
static TaskHandle_t s_decode_task = nullptr;

static int32_t get_data_frames(Frame *frame, int32_t frame_count) {
  for (int32_t i = 0; i < frame_count; i++)
    frame[i] = Frame(0);
  if (!s_bt_stream_playing)
    return frame_count;

  int32_t out = 0;
  while (out < frame_count && s_pcm_ring_read != s_pcm_ring_write) {
    int16_t L = s_pcm_ring[s_pcm_ring_read];
    s_pcm_ring_read = (s_pcm_ring_read + 1) % PCM_RING_SIZE;
    int16_t R = s_pcm_ring[s_pcm_ring_read];
    s_pcm_ring_read = (s_pcm_ring_read + 1) % PCM_RING_SIZE;
    frame[out++] = Frame(L, R);
  }
  return frame_count;
}

static int pcm_ring_space() {
  int r = s_pcm_ring_read;
  int w = s_pcm_ring_write;
  if (w >= r)
    return PCM_RING_SIZE - (w - r) - 1;
  return r - w - 1;
}

static void decode_task(void *arg) {
  mp3dec_t dec;
  mp3dec_init(&dec);
  uint8_t mp3_buf[2048];
  int16_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];

  while (s_bt_stream_playing && s_mp3_file) {
    int mp3_len = (int)fread(mp3_buf, 1, sizeof(mp3_buf), s_mp3_file);
    if (mp3_len <= 0)
      break;

    int mp3_pos = 0;
    while (mp3_pos < mp3_len && s_bt_stream_playing) {
      mp3dec_frame_info_t info;
      int samples = mp3dec_decode_frame(&dec, mp3_buf + mp3_pos,
                                        mp3_len - mp3_pos, pcm_buf, &info);
      if (samples <= 0 || info.frame_bytes <= 0)
        break;
      mp3_pos += info.frame_bytes;

      int total = samples * info.channels;
      if (info.channels == 1) {
        for (int i = samples - 1; i >= 0; i--) {
          pcm_buf[i * 2] = pcm_buf[i * 2 + 1] = pcm_buf[i];
        }
        total = samples * 2;
      }

      while (s_bt_stream_playing && pcm_ring_space() < total)
        vTaskDelay(pdMS_TO_TICKS(5));

      if (!s_bt_stream_playing)
        break;

      for (int i = 0; i < total; i++) {
        s_pcm_ring[s_pcm_ring_write] = pcm_buf[i];
        s_pcm_ring_write = (s_pcm_ring_write + 1) % PCM_RING_SIZE;
      }
    }
  }
  s_decode_task = nullptr;
  vTaskDelete(nullptr);
}

static void bt_stream_cleanup() {
  s_bt_stream_playing = false;
  if (s_decode_task) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (s_decode_task)
      vTaskDelete(s_decode_task);
    s_decode_task = nullptr;
  }
  if (s_mp3_file) {
    fclose(s_mp3_file);
    s_mp3_file = nullptr;
  }
  s_pcm_ring_read = 0;
  s_pcm_ring_write = 0;
}

static void button_handler(uint8_t key, bool isReleased) {
  if (isReleased)
    ESP_LOGI(BT_TAG, "🛜 Bluetooth button %d released", key);
}

void ESPWiFi::startBluetooth() {
  if (btStarted)
    return;
  if (s_scan_mutex == nullptr)
    s_scan_mutex = xSemaphoreCreateMutex();
  if (a2dp_source == nullptr)
    a2dp_source = new BluetoothA2DPSource();
  a2dp_source->set_data_callback_in_frames(get_data_frames);
  a2dp_source->set_avrc_passthru_command_callback(button_handler);
  a2dp_source->set_auto_reconnect(false);
  a2dp_source->start("My vision");
  btStarted = true;
  RegisterBluetoothHandlers();
  ensureLastPairedInDeviceList();
  log(INFO, "🛜 Bluetooth Started");
}

void ESPWiFi::stopBluetooth() {
  if (!btStarted)
    return;
  if (a2dp_source != nullptr) {
    a2dp_source->cancel_discovery();
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  UnregisterBluetoothHandlers();
  if (a2dp_source != nullptr) {
    a2dp_source->end(true);
    delete a2dp_source;
    a2dp_source = nullptr;
  }
  btStarted = false;
  connectBluetoothed = false;
  log(INFO, "🛜 Bluetooth Stopped");
}

void ESPWiFi::scanBluetooth(int timeout_sec) {
  if (!btStarted || a2dp_source == nullptr) {
    log(WARNING, "🛜 Bluetooth Scan skipped: not started");
    return;
  }
  if (s_scan_mutex == nullptr)
    s_scan_mutex = xSemaphoreCreateMutex();
  if (xSemaphoreTake(s_scan_mutex, portMAX_DELAY)) {
    s_scan_results.clear();
    xSemaphoreGive(s_scan_mutex);
  }
  a2dp_source->set_ssid_callback(bt_scan_collector);
  int sec = (timeout_sec <= 0 ? 1 : (timeout_sec > 12 ? 12 : timeout_sec));
  log(INFO, "🛜 Bluetooth Scanning (%d s)", sec);
  esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                             (uint8_t)sec, 0);
  if (err != ESP_OK) {
    log(WARNING, "🛜 Bluetooth Discovery failed: %s", esp_err_to_name(err));
    return;
  }
  vTaskDelay(pdMS_TO_TICKS((unsigned)sec * 1000));
  a2dp_source->cancel_discovery();
  /* Wait for library STACK_UP 10s delay to finish so we don't tear down
   * while the app task is blocked there (avoids "Starting device discovery..."
   * and set_scan_mode after controller is disabled). */
  vTaskDelay(pdMS_TO_TICKS(1500));
  a2dp_source->cancel_discovery();
  vTaskDelay(pdMS_TO_TICKS(400));
  if (xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(1000))) {
    s_btDiscoveredDevices = s_scan_results;
    xSemaphoreGive(s_scan_mutex);
  }
  ensureLastPairedInDeviceList();
  log(INFO, "🛜 Bluetooth Scan found %zu device(s)",
      s_btDiscoveredDevices.size());
}

static void bt_scan_task(void *arg) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(arg);
  espwifi->scanBluetooth(kBTScanDurationSec);
  s_scan_complete_flag = true;
  s_scan_task_handle = nullptr;
  vTaskDelete(nullptr);
}

void ESPWiFi::startBluetoothScanAsync() {
  if (!btStarted || a2dp_source == nullptr) {
    log(WARNING, "🛜 Bluetooth Scan skipped: not started");
    return;
  }
  if (s_scan_task_handle != nullptr) {
    log(INFO, "🛜 Bluetooth Scan already in progress");
    return;
  }
  if (s_scan_mutex == nullptr)
    s_scan_mutex = xSemaphoreCreateMutex();
  s_scan_complete_flag = false;
  BaseType_t ok = xTaskCreate(bt_scan_task, "bt_scan", 4096, this,
                              tskIDLE_PRIORITY + 1, &s_scan_task_handle);
  if (ok != pdPASS) {
    log(WARNING, "🛜 Bluetooth Scan task create failed");
    s_scan_task_handle = nullptr;
  }
}

bool ESPWiFi::isBluetoothScanInProgress() const {
  return (s_scan_task_handle != nullptr);
}

bool ESPWiFi::getAndClearBluetoothScanCompleteFlag() {
  if (!s_scan_complete_flag)
    return false;
  s_scan_complete_flag = false;
  return true;
}

void ESPWiFi::connectBluetooth(const std::string &nameOrNumberOrAddress) {
  if (!btStarted || a2dp_source == nullptr)
    return;
  std::string addr;
  if (nameOrNumberOrAddress.find(':') != std::string::npos) {
    addr = nameOrNumberOrAddress;
  } else {
    bool all_digits = true;
    for (size_t i = 0; i < nameOrNumberOrAddress.size(); i++) {
      if (!isdigit((unsigned char)nameOrNumberOrAddress[i])) {
        all_digits = false;
        break;
      }
    }
    if (all_digits && !nameOrNumberOrAddress.empty()) {
      int idx = atoi(nameOrNumberOrAddress.c_str());
      if (idx >= 1 && idx <= (int)s_btDiscoveredDevices.size())
        addr = s_btDiscoveredDevices[idx - 1].address;
    } else {
      for (const BTHost &h : s_btDiscoveredDevices) {
        if (h.name == nameOrNumberOrAddress) {
          addr = h.address;
          break;
        }
      }
    }
  }
  if (addr.empty()) {
    log(WARNING, "🛜 Bluetooth Connect: no match for '%s'",
        nameOrNumberOrAddress.c_str());
    return;
  }
  std::string name;
  for (const BTHost &h : s_btDiscoveredDevices) {
    if (h.address == addr) {
      name = h.name;
      break;
    }
  }
  // Persist so config has valid pointers (ArduinoJson stores ptr, not copy)
  static std::string s_last_attempt_addr, s_last_attempt_name;
  s_last_attempt_addr = addr;
  s_last_attempt_name = name.empty() ? addr : name;
  config["bluetooth"]["last_connect_attempt_address"] =
      s_last_attempt_addr.c_str();
  config["bluetooth"]["last_connect_attempt_name"] =
      s_last_attempt_name.c_str();

  esp_bd_addr_t bda;
  int a[6];
  if (sscanf(addr.c_str(), "%x:%x:%x:%x:%x:%x", &a[0], &a[1], &a[2], &a[3],
             &a[4], &a[5]) != 6) {
    log(WARNING, "🛜 Bluetooth Connect: invalid address '%s'", addr.c_str());
    return;
  }
  for (int i = 0; i < 6; i++)
    bda[i] = (uint8_t)a[i];
  log(INFO, "🛜 Bluetooth Connecting to %s", addr.c_str());
  a2dp_source->connect_to(bda);
}

void ESPWiFi::ensureLastPairedInDeviceList() {
  const char *addr =
      config["bluetooth"]["last_paired_address"].as<const char *>();
  if (!addr || !addr[0])
    return;
  std::string last_addr(addr);
  auto it = std::find_if(
      s_btDiscoveredDevices.begin(), s_btDiscoveredDevices.end(),
      [&last_addr](const BTHost &h) { return h.address == last_addr; });
  if (it != s_btDiscoveredDevices.end())
    return;
  const char *name = config["bluetooth"]["last_paired_name"].as<const char *>();
  BTHost host;
  host.address = last_addr;
  host.name = (name && name[0]) ? name : "Last paired";
  host.rssi = 0;
  s_btDiscoveredDevices.insert(s_btDiscoveredDevices.begin(), std::move(host));
}

size_t ESPWiFi::getDiscoveredDeviceCount() const {
  return s_btDiscoveredDevices.size();
}

std::string ESPWiFi::getDiscoveredDeviceName(size_t index) const {
  if (index >= s_btDiscoveredDevices.size())
    return "";
  return s_btDiscoveredDevices[index].name;
}

std::string ESPWiFi::getDiscoveredDeviceAddress(size_t index) const {
  if (index >= s_btDiscoveredDevices.size())
    return "";
  return s_btDiscoveredDevices[index].address;
}

int ESPWiFi::getDiscoveredDeviceRssi(size_t index) const {
  if (index >= s_btDiscoveredDevices.size())
    return 0;
  return s_btDiscoveredDevices[index].rssi;
}

void ESPWiFi::startBluetoothStream() {
  bt_stream_cleanup();
  initSDCard();
  s_mp3_file = fopen(kBTStreamMp3Path, "rb");
  if (!s_mp3_file) {
    log(WARNING, "🛜 Cannot open %s", kBTStreamMp3Path);
    return;
  }
  s_pcm_ring_read = 0;
  s_pcm_ring_write = 0;
  s_bt_stream_playing = true;
  BaseType_t ok = xTaskCreate(decode_task, "mp3_decode", 4096, nullptr,
                              tskIDLE_PRIORITY + 2, &s_decode_task);
  if (ok != pdPASS) {
    log(WARNING, "🛜 Failed to create decode task");
    bt_stream_cleanup();
    return;
  }
  log(INFO, "🛜 Bluetooth stream started: %s", kBTStreamMp3Path);
}

void ESPWiFi::stopBluetoothStream() {
  bt_stream_cleanup();
  log(INFO, "🛜 Bluetooth stream stopped");
}

bool ESPWiFi::isBluetoothStreamPlaying() const { return s_bt_stream_playing; }

void BTHost::connect(ESPWiFi *espwifi) const {
  if (espwifi)
    espwifi->connectBluetooth(address);
}

#endif // ESPWiFi_BLUETOOTH_H

#endif // ESPWiFi_BT_ENABLED && CONFIG_BT_A2DP_ENABLE

void ESPWiFi::bluetoothConfigHandler() {
#if defined(ESPWiFi_BT_ENABLED) && defined(CONFIG_BT_A2DP_ENABLE)
  static bool lastEnabled = config["bluetooth"]["enabled"].as<bool>();
  bool currentEnabled = config["bluetooth"]["enabled"].as<bool>();
  if (currentEnabled != lastEnabled) {
    lastEnabled = currentEnabled;
    if (currentEnabled)
      startBluetooth();
    else
      stopBluetooth();
  }
#endif
}
