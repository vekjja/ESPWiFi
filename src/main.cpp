#include "ESPWiFi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#if ESPWiFi_HAS_TFT
#include "ui/ui.h"
#endif

ESPWiFi espwifi;

#if ESPWiFi_HAS_TFT
static void uiUpdateBluetoothInfo(std::string info) {
  if (ui_BluetoothInfoLabel) {
    lv_label_set_text(
        ui_BluetoothInfoLabel,
        info.empty() ? "Ensure the Remote Device is in Pairing Mode and Nearby"
                     : info.c_str());
  }
}

void uiUpdateBluetoothDropdown(const std::vector<std::string>& devices,
                               std::string selectedDevice = "") {
  if (ui_BluetoothDropdown) {
    lv_dropdown_clear_options(ui_BluetoothDropdown);
    for (size_t i = 0; i < espwifi.bluetoothScannedHosts.size(); i++) {
      lv_dropdown_add_option(ui_BluetoothDropdown,
                             espwifi.bluetoothScannedHosts[i].c_str(),
                             (uint32_t)i);
    }
  }
}

static void uiDropdownHandler(lv_event_t* evt) {
  if (lv_event_get_code(evt) == LV_EVENT_CLICKED) {
    uiUpdateBluetoothDropdown(espwifi.bluetoothScannedHosts);
    return;
  }
  if (lv_event_get_code(evt) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  if (!ui_BluetoothDropdown) {
    return;
  }
  uint32_t selection = lv_dropdown_get_selected(ui_BluetoothDropdown);
  // LVGL returns (uint32_t)-1 (0xFFFFFFFF) when no option is selected
  if (selection != (uint32_t)-1) {
    espwifi.log(INFO, "📱 Bluetooth dropdown Selected: %lu device %s",
                (unsigned long)selection,
                espwifi.bluetoothScannedHosts[selection].c_str());
    espwifi.bluetoothConnectTargetName =
        espwifi.bluetoothScannedHosts[selection].c_str();
  }
}

static void uiPlayButtonClicked(lv_event_t* evt) {
  if (lv_event_get_code(evt) != LV_EVENT_CLICKED) {
    return;
  }
  static bool playing = false;
  espwifi.log(INFO, "🛜🎵 Play button pressed");

  if (playing) {
    playing = false;
    lv_obj_clear_state(ui_PlayButton, LV_STATE_CHECKED);
    espwifi.stopBluetoothWavPlayback();
    return;
  } else {
    playing = true;
    lv_obj_add_state(ui_PlayButton, LV_STATE_CHECKED);
    espwifi.startBluetoothWavPlayback("/sd/music/test.wav");
  }
}
#endif

extern "C" void app_main(void) {
#if ESPWiFi_HAS_TFT
  espwifi.onBluetoothConnectionStateChanged =
      [](esp_a2d_connection_state_t state) {
        switch (state) {
          case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            uiUpdateBluetoothInfo("Disconnected");
            break;
          case ESP_A2D_CONNECTION_STATE_CONNECTING:
            uiUpdateBluetoothInfo("Connecting...");
            break;
          case ESP_A2D_CONNECTION_STATE_CONNECTED:
            uiUpdateBluetoothInfo("Connected\n" +
                                  espwifi.bluetoothConnectTargetName);
            break;
          case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            uiUpdateBluetoothInfo("Disconnecting...");
            break;
          default:
            break;
        }
      };
  espwifi.registerUiEventHandlers = [](ESPWiFi* esp) {
    if (ui_BluetoothDropdown) {
      lv_obj_add_event_cb(ui_BluetoothDropdown, uiDropdownHandler,
                          LV_EVENT_VALUE_CHANGED, esp);
      lv_obj_add_event_cb(ui_BluetoothDropdown, uiDropdownHandler,
                          LV_EVENT_CLICKED, esp);
    }
    if (ui_PlayButton) {
      lv_obj_add_event_cb(ui_PlayButton, uiPlayButtonClicked, LV_EVENT_CLICKED,
                          esp);
    }
  };
#endif
  espwifi.start();
  uiUpdateBluetoothInfo("");
  uiUpdateBluetoothDropdown(espwifi.bluetoothScannedHosts);
  espwifi.runSystem();
}
