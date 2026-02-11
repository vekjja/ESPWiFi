
#include "ESPWiFi.h"
#if defined(ESPWiFi_BT_ENABLED) && defined(CONFIG_BT_A2DP_ENABLE)

#ifndef ESPWiFi_BLUETOOTH_H
#define ESPWiFi_BLUETOOTH_H
#ifdef ESPWiFi_BT_ENABLED

#include "BluetoothA2DPSource.h"

static ESPWiFi *s_bt_espwifi = nullptr;

static bool onSsidDiscovered(const char *ssid, esp_bd_addr_t address,
                             int rssi) {
  if (!s_bt_espwifi || !ssid)
    return false;
  bool already = false;
  for (const auto &s : s_bt_espwifi->bluetoothScannedHosts)
    if (s == ssid) {
      already = true;
      break;
    }
  if (!already)
    s_bt_espwifi->bluetoothScannedHosts.push_back(ssid);
  if (s_bt_espwifi->onBluetoothDeviceDiscovered)
    return s_bt_espwifi->onBluetoothDeviceDiscovered(ssid, address, rssi);
  return false;
}

void ESPWiFi::startBluetooth() {
  if (a2dp_source != nullptr) {
    log(INFO, "🛜 Bluetooth already started");
    return;
  }
  s_bt_espwifi = this;
  a2dp_source = new BluetoothA2DPSource();
  a2dp_source->set_ssid_callback(onSsidDiscovered);
  a2dp_source->start(); // controller + bluedroid + A2DP init, discoverable
  log(INFO, "🛜 Bluetooth Started");
}

void ESPWiFi::stopBluetooth() {
  if (a2dp_source == nullptr)
    return;
  a2dp_source->end();
  delete a2dp_source;
  a2dp_source = nullptr;
  s_bt_espwifi = nullptr;
  log(INFO, "🛜 Bluetooth Stopped");
}

void ESPWiFi::toggleBluetooth() {
  if (a2dp_source == nullptr) {
    startBluetooth();
  } else {
    stopBluetooth();
  }
}

#endif
#endif // ESPWiFi_BLUETOOTH_H

#elif defined(CONFIG_BT_A2DP_ENABLE)
// Stubs when CONFIG_BT_A2DP_ENABLE but not ESPWiFi_BT_ENABLED
void ESPWiFi::startBluetooth() { log(WARNING, "🛜 Bluetooth Not Implemented"); }
void ESPWiFi::stopBluetooth() { log(WARNING, "🛜 Bluetooth Not Implemented"); }
void ESPWiFi::toggleBluetooth() {
  log(WARNING, "🛜 Bluetooth Not Implemented");
}
#endif
