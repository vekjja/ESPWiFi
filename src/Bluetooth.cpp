
#ifndef ESPWiFi_BLUETOOTH_H
#define ESPWiFi_BLUETOOTH_H

#include "ESPWiFi.h"
#if defined(CONFIG_BT_A2DP_ENABLE)

#include <cstring>

#define MINIMP3_IMPLEMENTATION
#include "BluetoothA2DPSource.h"

static ESPWiFi* bt_espwifi = nullptr;

// Non-static so BluetoothWav.cpp can reference them via extern.
BluetoothA2DPSource s_a2dp_source;

int32_t silentDataCb(uint8_t* data, int32_t len) {
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

static void onAudioStateChanged(esp_a2d_audio_state_t state, void* obj) {
  (void)obj;
  if (bt_espwifi) {
    bt_espwifi->log(INFO, "🛜 Bluetooth audio state changed: %d", state);
    if (bt_espwifi->onBluetoothAudioStateChanged) {
      bt_espwifi->onBluetoothAudioStateChanged(state);
    }
  }
}

static void onDiscoveryModeChanged(esp_bt_gap_discovery_state_t state) {
  if (bt_espwifi && bt_espwifi->onBluetoothDiscoveryStateChanged)
    bt_espwifi->onBluetoothDiscoveryStateChanged(state);
}

void ESPWiFi::startBluetooth() {
  if (a2dp_source != nullptr) {
    log(INFO, "🛜 Bluetooth already started");
    return;
  }
  bt_espwifi = this;
  s_a2dp_source.set_data_callback(silentDataCb);
  s_a2dp_source.set_ssid_callback(onSsidDiscovered);
  s_a2dp_source.set_on_audio_state_changed(onAudioStateChanged);
  s_a2dp_source.set_discovery_mode_callback(onDiscoveryModeChanged);
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

void ESPWiFi::stopBluetoothMp3Playback() {}

#endif  // CONFIG_BT_A2DP_ENABLE
#endif  // ESPWiFi_BLUETOOTH_H
