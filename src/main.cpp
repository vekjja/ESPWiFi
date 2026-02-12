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
    lv_label_set_text(ui_BluetoothInfoLabel,
                      info.empty() ? "Ensure Your Device is in Pairing Mode"
                                   : info.c_str());
  }
  if (ui_BluetoothDropdown) {
    lv_dropdown_clear_options(ui_BluetoothDropdown);
    for (size_t i = 0; i < espwifi.bluetoothScannedHosts.size(); i++) {
      lv_dropdown_add_option(ui_BluetoothDropdown,
                             espwifi.bluetoothScannedHosts[i].c_str(),
                             (uint32_t)i);
      espwifi.log(INFO, "Adding Bluetooth device: %s",
                  espwifi.bluetoothScannedHosts[i].c_str());
    }
  }
}

static void uiDropdownHandler(lv_event_t *evt) {

  if (lv_event_get_code(evt) == LV_EVENT_CLICKED) {
    uiUpdateBluetoothInfo("Scanning");
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
    uiUpdateBluetoothInfo("Connecting:\n" +
                          espwifi.bluetoothScannedHosts[selection]);
    espwifi.log(INFO, "📱 Bluetooth dropdown Selected: %lu device %s",
                (unsigned long)selection,
                espwifi.bluetoothScannedHosts[selection].c_str());
    espwifi.bluetoothConnectTargetName =
        espwifi.bluetoothScannedHosts[selection].c_str();
  }
}

static void uiPlayButtonClicked(lv_event_t *evt) {
  if (lv_event_get_code(evt) != LV_EVENT_CLICKED) {
    return;
  }
  espwifi.log(INFO, "🛜🎵 Play button pressed");
  static const char *kDefaultMp3Path = "/sd/music/we r.mp3";
  espwifi.startBluetoothMp3Playback(kDefaultMp3Path);
}
#endif

extern "C" void app_main(void) {
#if ESPWiFi_HAS_TFT
  espwifi.registerUiEventHandlers = [](ESPWiFi *esp) {
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
    uiUpdateBluetoothInfo("");
  };
#endif
  espwifi.start();
  for (;;) {
    espwifi.runSystem();
    espwifi.feedWatchDog();
  }
}
