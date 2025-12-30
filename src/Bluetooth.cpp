#ifndef ESPWiFi_BLUETOOTH_H
#define ESPWiFi_BLUETOOTH_H

#include "BluetoothA2DPSource.h"
#include "ESPWiFi.h"
#include "esp_log.h"
#include <cstring>

static const char *BT_TAG = "ESPWiFi_BT";

BluetoothA2DPSource a2dp_source;

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
  if (bluetoothStarted) {
    return;
  }
  a2dp_source.set_data_callback(get_data);
  a2dp_source.set_avrc_passthru_command_callback(button_handler);
  a2dp_source.start("My vision");
  bluetoothStarted = true;
  log(INFO, "🛜✅ Bluetooth Started");
}

void ESPWiFi::stopBluetooth() {
  if (!bluetoothStarted) {
    return;
  }
  a2dp_source.end();
  bluetoothStarted = false;
  log(INFO, "🛜🛑 Bluetooth Stopped");
}

#endif // ESPWiFi_BLUETOOTH_H