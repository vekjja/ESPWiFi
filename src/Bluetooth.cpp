
#include "ESPWiFi.h"
#if defined(ESPWiFi_BT_ENABLED) && defined(CONFIG_BT_A2DP_ENABLE)

#ifndef ESPWiFi_BLUETOOTH_H
#define ESPWiFi_BLUETOOTH_H
#ifdef ESPWiFi_BT_ENABLED

#include "BluetoothA2DPSource.h"
#include <cstring>

static ESPWiFi *bt_espwifi = nullptr;
static BluetoothA2DPSource s_a2dp_source;

static int32_t silentDataCb(uint8_t *data, int32_t len) {
  if (data && len > 0) {
    memset(data, 0, (size_t)len);
    return len;
  }
  return 0;
}

static bool onSsidDiscovered(const char *ssid, esp_bd_addr_t address,
                             int rssi) {
  if (!bt_espwifi || !ssid)
    return false;
  if (!bt_espwifi->bluetoothConnectTargetName.empty() &&
      bt_espwifi->bluetoothConnectTargetName == ssid) {
    bt_espwifi->bluetoothConnectTargetName.clear();
    return true;
  }
  bool alreadyInList = false;
  for (const auto &s : bt_espwifi->bluetoothScannedHosts)
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

void ESPWiFi::startBluetooth() {
  if (a2dp_source != nullptr) {
    log(INFO, "🛜 Bluetooth already started");
    return;
  }
  bt_espwifi = this;
  s_a2dp_source.set_data_callback(silentDataCb);
  s_a2dp_source.set_ssid_callback(onSsidDiscovered);
  s_a2dp_source.start(getHostname().c_str());
  a2dp_source = &s_a2dp_source;
  log(INFO, "🛜 Bluetooth Started");
}

void ESPWiFi::stopBluetooth() {
  if (a2dp_source == nullptr)
    return;
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

void ESPWiFi::startBluetoothMp3Playback(const char *path) {
  (void)path;
  log(WARNING, "🛜🎵 MP3 playback not implemented");
}

void ESPWiFi::stopBluetoothMp3Playback() {}

#endif
#endif // ESPWiFi_BLUETOOTH_H

#elif defined(CONFIG_BT_A2DP_ENABLE)
// Stubs when CONFIG_BT_A2DP_ENABLE but not ESPWiFi_BT_ENABLED
void ESPWiFi::startBluetooth() { log(WARNING, "🛜 Bluetooth Not Implemented"); }
void ESPWiFi::stopBluetooth() { log(WARNING, "🛜 Bluetooth Not Implemented"); }
void ESPWiFi::startBluetoothMp3Playback(const char *) {}
void ESPWiFi::stopBluetoothMp3Playback() {}
void ESPWiFi::toggleBluetooth() {
  log(WARNING, "🛜 Bluetooth Not Implemented");
}
#endif
