#ifndef ESPWiFi_BLUETOOTH_HANDLERS
#define ESPWiFi_BLUETOOTH_HANDLERS

#ifdef CONFIG_BT_CLASSIC_ENABLED

#include "BluetoothA2DPSource.h"
#include "ESPWiFi.h"
#include "esp_a2dp_api.h"
#include "esp_log.h"

#define ESPWiFi_OBJ_CAST(obj)                                                  \
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(obj);                              \
  if (!espwifi) {                                                              \
    return;                                                                    \
  }

static const char *BT_HANDLER_TAG = "ESPWiFi_BT_Handler";

// Forward declaration - a2dp_source is defined in Bluetooth.cpp
extern BluetoothA2DPSource *a2dp_source;

// ========== Bluetooth Event Handler Callbacks ==========

// Connection state change callback (instance method)
void ESPWiFi::bluetoothConnectionSC(esp_a2d_connection_state_t state,
                                    void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  const char *stateStr = "UNKNOWN";
  switch (state) {
  case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
    stateStr = "DISCONNECTED";
    espwifi->connectBluetoothed = false;
    espwifi->log(INFO, "🛜 Bluetooth Disconnected ⛓️‍💥");
    break;
  case ESP_A2D_CONNECTION_STATE_CONNECTING:
    stateStr = "CONNECTING";
    espwifi->connectBluetoothed = false;
    espwifi->log(INFO, "🛜 Bluetooth Connecting... 🔄");
    break;
  case ESP_A2D_CONNECTION_STATE_CONNECTED:
    stateStr = "CONNECTED";
    espwifi->connectBluetoothed = true;
    espwifi->log(INFO, "🛜 Bluetooth Connected 🔗");
    break;
  case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
    stateStr = "DISCONNECTING";
    espwifi->connectBluetoothed = false;
    espwifi->log(INFO, "🛜 Bluetooth Disconnecting... ⏳");
    break;
  default:
    break;
  }

  ESP_LOGI(BT_HANDLER_TAG, "Connection state: %s", stateStr);
}

// Audio state change callback (instance method)
void ESPWiFi::btAudioStateChange(esp_a2d_audio_state_t state, void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  const char *stateStr = "UNKNOWN";
  switch (state) {
    //   case ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND:
    //     stateStr = "REMOTE_SUSPEND";
    //     espwifi->log(INFO, "🛜⏸️ Bluetooth Audio Suspended (Remote)");
    //     break;
  case ESP_A2D_AUDIO_STATE_STOPPED:
    stateStr = "STOPPED";
    espwifi->log(INFO, "🛜⏹️ Bluetooth Audio Stopped");
    break;
  case ESP_A2D_AUDIO_STATE_STARTED:
    stateStr = "STARTED";
    espwifi->log(INFO, "🛜▶️ Bluetooth Audio Started");
    break;
  default:
    break;
  }

  ESP_LOGI(BT_HANDLER_TAG, "Audio state: %s", stateStr);
}

// ========== Bluetooth Handler Registration ==========

esp_err_t ESPWiFi::RegisterBluetoothHandlers() {
  // Only register if Bluetooth is started and a2dp_source exists
  if (a2dp_source == nullptr) {
    ESP_LOGW(BT_HANDLER_TAG, "Cannot register handlers: a2dp_source is null");
    return ESP_ERR_INVALID_STATE;
  }

  // Register connection state callback
  // The library expects a function pointer: void
  // (*)(esp_a2d_connection_state_t, void*)
  a2dp_source->set_on_connection_state_changed(bluetoothConnectionSCStatic,
                                               this);

  // Register audio state callback
  // The library expects a function pointer: void (*)(esp_a2d_audio_state_t,
  // void*)
  a2dp_source->set_on_audio_state_changed(btAudioStateChangeStatic, this);

  ESP_LOGI(BT_HANDLER_TAG, "Bluetooth event handlers registered");
  return ESP_OK;
}

void ESPWiFi::UnregisterBluetoothHandlers() {
  // Note: ESP32-A2DP library doesn't provide unregister methods,
  // but we can set callbacks to nullptr if needed
  if (a2dp_source != nullptr) {
    // The library doesn't expose a way to clear callbacks,
    // so we just log that handlers are being cleared conceptually
    ESP_LOGI(BT_HANDLER_TAG, "Bluetooth event handlers cleared");
  }
}

// Static wrapper functions to forward to instance methods
// These match the callback signature expected by the library
void ESPWiFi::bluetoothConnectionSCStatic(esp_a2d_connection_state_t state,
                                          void *obj) {
  ESPWiFi_OBJ_CAST(obj);
  espwifi->bluetoothConnectionSC(state, obj);
}

void ESPWiFi::btAudioStateChangeStatic(esp_a2d_audio_state_t state, void *obj) {
  ESPWiFi_OBJ_CAST(obj);
  espwifi->btAudioStateChange(state, obj);
}

#endif // CONFIG_BT_CLASSIC_ENABLED

#endif // ESPWiFi_BLUETOOTH_HANDLERS
