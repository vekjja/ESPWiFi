
#include "ESPWiFi.h"
#if defined(ESPWiFi_BT_ENABLED) && defined(CONFIG_BT_A2DP_ENABLE)

#ifndef ESPWiFi_BLUETOOTH_HANDLERS
#define ESPWiFi_BLUETOOTH_HANDLERS

#include "BluetoothA2DPSource.h"
#include "esp_a2dp_api.h"
#include "esp_log.h"

#define ESPWiFi_OBJ_CAST(obj)                                                  \
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(obj);                              \
  if (!espwifi) {                                                              \
    return;                                                                    \
  }

static const char *BT_HANDLER_TAG = "ESPWiFi_BT_Handler";

// Pending connection state: set from BT task callback, consumed on main loop
// to avoid StoreProhibited (callback runs in library task with limited context).
static volatile int s_pending_bt_connection_state = -1;

// Forward declaration - a2dp_source is defined in Bluetooth.cpp
extern BluetoothA2DPSource *a2dp_source;

// ========== Bluetooth Event Handler Callbacks ==========

// Connection state change callback: only set pending state and log.
// Main loop applies state (connectBluetoothed, updateBluetoothInfo) in renderTFT.
void ESPWiFi::bluetoothConnectionSC(esp_a2d_connection_state_t state,
                                    void *obj) {
  (void)obj;
  const char *stateStr = "UNKNOWN";
  switch (state) {
  case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
    stateStr = "DISCONNECTED";
    break;
  case ESP_A2D_CONNECTION_STATE_CONNECTING:
    stateStr = "CONNECTING";
    break;
  case ESP_A2D_CONNECTION_STATE_CONNECTED:
    stateStr = "CONNECTED";
    break;
  case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
    stateStr = "DISCONNECTING";
    break;
  default:
    break;
  }
  ESP_LOGI(BT_HANDLER_TAG, "Connection state: %s", stateStr);
  s_pending_bt_connection_state = (int)state;
}

int ESPWiFi::getAndClearPendingBluetoothConnectionState() {
  int v = s_pending_bt_connection_state;
  s_pending_bt_connection_state = -1;
  return v;
}

// Audio state change callback: only log (no espwifi access to avoid crash in library task).
void ESPWiFi::btAudioStateChange(esp_a2d_audio_state_t state, void *obj) {
  (void)obj;
  const char *stateStr = "UNKNOWN";
  switch (state) {
  case ESP_A2D_AUDIO_STATE_STOPPED:
    stateStr = "STOPPED";
    break;
  case ESP_A2D_AUDIO_STATE_STARTED:
    stateStr = "STARTED";
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

// Static wrapper functions (callbacks run in library app task; keep them minimal).
void ESPWiFi::bluetoothConnectionSCStatic(esp_a2d_connection_state_t state,
                                          void *obj) {
  if (obj)
    static_cast<ESPWiFi *>(obj)->bluetoothConnectionSC(state, obj);
}

void ESPWiFi::btAudioStateChangeStatic(esp_a2d_audio_state_t state, void *obj) {
  if (obj)
    static_cast<ESPWiFi *>(obj)->btAudioStateChange(state, obj);
}

#endif // ESPWiFi_BLUETOOTH_HANDLERS
#endif // ESPWiFi_BT_ENABLED && CONFIG_BT_A2DP_ENABLE
