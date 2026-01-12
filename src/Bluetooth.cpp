
#include "ESPWiFi.h"
#ifdef ESPWiFi_BT_ENABLED

#ifndef ESPWiFi_BLUETOOTH_H
#define ESPWiFi_BLUETOOTH_H

#include "BluetoothA2DPSource.h"
#include "esp_log.h"
#include <cstring>

static const char *BT_TAG = "ESPWiFi_BT";

BluetoothA2DPSource *a2dp_source = nullptr;

// Callback function to provide audio data to A2DP source
int32_t get_data(uint8_t *data, int32_t len) {
  // Fill with silence for now
  memset(data, 0, len);
  return len;
}

// Callback function for AVRCP button presses on remote Bluetooth speaker
void button_handler(uint8_t key, bool isReleased) {
  if (isReleased) {
    ESP_LOGI(BT_TAG, "🛜 Bluetooth button %d released", key);
  }
}

void ESPWiFi::startBluetooth() {
  if (btStarted) {
    return;
  }
  // Allocate Bluetooth object only when starting
  if (a2dp_source == nullptr) {
    a2dp_source = new BluetoothA2DPSource();
  }
  a2dp_source->set_data_callback(get_data);
  a2dp_source->set_avrc_passthru_command_callback(button_handler);
  a2dp_source->start("My vision");
  btStarted = true;
  // Register event handlers after Bluetooth is started
  RegisterBluetoothHandlers();
  log(INFO, "🛜 Bluetooth Started");
}

void ESPWiFi::stopBluetooth() {
  if (!btStarted) {
    return;
  }
  // Unregister handlers before stopping
  UnregisterBluetoothHandlers();

  if (a2dp_source != nullptr) {
    a2dp_source->end(true); // release memory
    delete a2dp_source;
    a2dp_source = nullptr;
  }
  btStarted = false;
  connectBluetoothed = false;
  log(INFO, "🛜 Bluetooth Stopped");
}

void ESPWiFi::scanBluetooth() {
  if (!btStarted || a2dp_source == nullptr) {
    log(WARNING, "🛜 Bluetooth Skipping scan: not started or not initialized");
    return;
  }
  log(INFO, "🛜 Bluetooth Scanning");
}

void ESPWiFi::connectBluetooth(const std::string &address) {
  if (!btStarted) {
    return;
  }
  log(INFO, "🛜 Bluetooth Connecting to %s", address.c_str());
}

#endif // ESPWiFi_BLUETOOTH_H

#endif // ESPWiFi_BT_ENABLED

void ESPWiFi::bluetoothConfigHandler() {
#ifdef CONFIG_BT_A2DP_ENABLE
  static bool lastEnabled = config["bluetooth"]["enabled"].as<bool>();
  static bool currentEnabled = config["bluetooth"]["enabled"].as<bool>();

  currentEnabled = config["bluetooth"]["enabled"].as<bool>();
  if (currentEnabled != lastEnabled) {
    if (currentEnabled) {
      startBluetooth();
    } else {
      stopBluetooth();
    }
  }
#endif
}