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

void uiUpdateTitle(std::string title) {
  if (ui_Title) {
    lv_label_set_text(ui_Title, title.c_str());
  }
}

static void uiUpdateInfo(std::string info) {
  if (ui_InfoLabel) {
    lv_label_set_text(ui_InfoLabel, info.c_str());
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
    uiUpdateInfo("");
    return;
  } else {
    playing = true;
    uiUpdateInfo("Chance Of Rain");
    lv_obj_add_state(ui_PlayButton, LV_STATE_CHECKED);
    espwifi.startBluetoothWavPlayback(
        "/sd/music/chance-of-rain-albert-behar.wav");
  }
}
#endif

extern "C" void app_main(void) {
#if ESPWiFi_HAS_TFT
  espwifi.onBluetoothDeviceDiscovered = [](const char* name,
                                           esp_bd_addr_t address, int rssi) {
    return true;  // connect to first discovered device
  };
  espwifi.onBluetoothConnectionStateChanged =
      [](esp_a2d_connection_state_t state) {
        switch (state) {
          case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            uiUpdateInfo("Disconnected");
            break;
          case ESP_A2D_CONNECTION_STATE_CONNECTING:
            uiUpdateInfo("Connecting...");
            break;
          case ESP_A2D_CONNECTION_STATE_CONNECTED:
            lv_obj_remove_flag(ui_PlayButton, LV_OBJ_FLAG_HIDDEN);
            uiUpdateInfo("Connected");
            break;
          case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            uiUpdateInfo("Disconnecting...");
            break;
          default:
            break;
        }
      };
  espwifi.registerUiEventHandlers = [](ESPWiFi* esp) {
    if (ui_PlayButton) {
      lv_obj_add_event_cb(ui_PlayButton, uiPlayButtonClicked, LV_EVENT_CLICKED,
                          esp);
    }
  };
#endif
  espwifi.start();
  uiUpdateTitle("Albert Behar\n");
  lv_obj_add_flag(ui_PlayButton, LV_OBJ_FLAG_HIDDEN);
  uiUpdateInfo("Ensure the Remote Device is in Pairing Mode and Nearby");
  espwifi.runSystem();
}
