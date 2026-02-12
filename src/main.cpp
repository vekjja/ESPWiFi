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
static void updateBluetoothInfo(ESPWiFi *ew, std::string info) {
  if (ui_BluetoothInfoLabel) {
    lv_label_set_text(ui_BluetoothInfoLabel,
                      info.empty() ? "Click BT Button to Update Device List"
                                   : info.c_str());
  }
  if (ui_BluetoothDropdown) {
    lv_dropdown_clear_options(ui_BluetoothDropdown);
    for (size_t i = 0; i < ew->bluetoothScannedHosts.size(); i++) {
      lv_dropdown_add_option(ui_BluetoothDropdown,
                             ew->bluetoothScannedHosts[i].c_str(), (uint32_t)i);
      ew->log(INFO, "Adding Bluetooth device: %s",
              ew->bluetoothScannedHosts[i].c_str());
    }
  }
}

static void onBluetoothButtonClicked(lv_event_t *e) {
  espwifi.log(INFO, "Bluetooth button pressed");
  void *ud = lv_event_get_user_data(e);
  if (ud) {
    ESPWiFi *ew = static_cast<ESPWiFi *>(ud);
    ew->feedWatchDog();
    updateBluetoothInfo(ew, "Scanning...");
  }
}

static void onBluetoothDropdownChanged(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  if (!ui_BluetoothDropdown) {
    return;
  }
  ESPWiFi *ew = static_cast<ESPWiFi *>(lv_event_get_user_data(e));
  if (ew) {
    uint32_t sel = lv_dropdown_get_selected(ui_BluetoothDropdown);
    // LVGL returns (uint32_t)-1 (0xFFFFFFFF) when no option is selected
    if (sel != (uint32_t)-1) {
      ew->log(INFO, "📱 Bluetooth dropdown Selected: %lu device %s",
              (unsigned long)sel, ew->bluetoothScannedHosts[sel].c_str());
      ew->bluetoothConnectTargetName = ew->bluetoothScannedHosts[sel].c_str();
    }
  }
}

static void onPlayButtonClicked(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  ESPWiFi *ew = static_cast<ESPWiFi *>(lv_event_get_user_data(e));
  if (ew) {
    ew->log(INFO, "🛜🎵 Play button pressed");
    static const char *kDefaultMp3Path = "/sd/music/we r.mp3";
    ew->startBluetoothMp3Playback(kDefaultMp3Path);
  }
}
#endif

extern "C" void app_main(void) {
#if ESPWiFi_HAS_TFT
  espwifi.registerUiEventHandlers = [](ESPWiFi *ew) {
    if (ui_BluetoothButton) {
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
