#include "ESPWiFi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#if ESPWiFi_HAS_TFT
#include "ui/ui.h"
#endif

static const char *TAG = "main";

ESPWiFi espwifi;

#if ESPWiFi_HAS_TFT
static void updateBluetoothInfo(ESPWiFi *ew, std::string info) {
  if (ui_BluetoothInfoLabel) {
    lv_label_set_text(ui_BluetoothInfoLabel,
                      info.empty() ? "Click BT Button to Scan for Devices"
                                   : info.c_str());
  }
  if (ui_BluetoothDropdown) {
    lv_dropdown_clear_options(ui_BluetoothDropdown);
#ifdef CONFIG_BT_A2DP_ENABLE
    for (size_t i = 0; i < ew->bluetoothScannedHosts.size(); i++) {
      lv_dropdown_add_option(ui_BluetoothDropdown,
                             ew->bluetoothScannedHosts[i].c_str(), (uint32_t)i);
    }
#endif
  }
}

static void onBluetoothButtonClicked(lv_event_t *e) {
  ESP_LOGI(TAG, "Bluetooth button pressed");
  void *ud = lv_event_get_user_data(e);
  if (ud) {
    ESPWiFi *ew = static_cast<ESPWiFi *>(ud);
    ew->feedWatchDog();
#ifdef CONFIG_BT_A2DP_ENABLE
    ew->startBluetooth();
#endif
    updateBluetoothInfo(ew, "");
  }
}

static void onBluetoothDropdownChanged(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    return;
  if (!ui_BluetoothDropdown)
    return;
  ESPWiFi *ew = static_cast<ESPWiFi *>(lv_event_get_user_data(e));
  if (ew) {
    uint32_t sel = lv_dropdown_get_selected(ui_BluetoothDropdown);
    ew->log(INFO, "🎵 Bluetooth dropdown Selected: %lu", (unsigned long)sel);
  }
}

static void onPlayButtonClicked(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  ESPWiFi *ew = static_cast<ESPWiFi *>(lv_event_get_user_data(e));
  if (ew)
    ew->log(INFO, "🎵 Play button pressed");
}
#endif

extern "C" void app_main(void) {
#if ESPWiFi_HAS_TFT
  espwifi.registerUiEventHandlers = [](ESPWiFi *ew) {
    if (ui_BluetoothButton) {
      ESP_LOGI(TAG, "Registering UI event handler for Bluetooth button");
      lv_obj_add_event_cb(ui_BluetoothButton, onBluetoothButtonClicked,
                          LV_EVENT_CLICKED, ew);
      updateBluetoothInfo(ew, "");
    }
    if (ui_BluetoothDropdown) {
      lv_obj_add_event_cb(ui_BluetoothDropdown, onBluetoothDropdownChanged,
                          LV_EVENT_VALUE_CHANGED, ew);
    }
    if (ui_PlayButton) {
      lv_obj_add_event_cb(ui_PlayButton, onPlayButtonClicked, LV_EVENT_CLICKED,
                          ew);
    }
  };
#endif
  espwifi.start();
  for (;;) {
    espwifi.runSystem();
    espwifi.feedWatchDog();
  }
}
