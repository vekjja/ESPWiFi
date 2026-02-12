#ifndef UI_CPP
#define UI_CPP

#if ESPWiFi_HAS_TFT
static void onBluetoothButtonClicked(lv_event_t *e) {
  ESP_LOGI(TAG, "Bluetooth button pressed");
  void *ud = lv_event_get_user_data(e);
  if (ud) {
    ESPWiFi *ew = static_cast<ESPWiFi *>(ud);
    ew->feedWatchDog();
#ifdef CONFIG_BT_A2DP_ENABLE
    ew->startBluetooth();
#endif
    ew->updateBluetoothInfo();
  }
}

static void onBluetoothDropdownChanged(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    return;
  if (!ui_BluetoothDropdown)
    return;
  ESPWiFi *ew = static_cast<ESPWiFi *>(lv_event_get_user_data(e));
  if (!ew)
    return;
  uint32_t sel = lv_dropdown_get_selected(ui_BluetoothDropdown);
  ew->log(INFO, "🎵 Bluetooth dropdown Selected: %lu", (unsigned long)sel);
}

static void onPlayButtonClicked(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    return;
  ESPWiFi *ew = static_cast<ESPWiFi *>(lv_event_get_user_data(e));
  if (ew)
    ew->log(INFO, "🎵 Play button pressed");
}
#endif

#endif // UI_CPP