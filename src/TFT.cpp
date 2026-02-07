// TFT.cpp - LVGL display with esp_lcd backend

#include "ESPWiFi.h"

#if ESPWiFi_HAS_TFT

#include "Touch.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"
#include "lvgl.h"
#include "src/draw/sw/lv_draw_sw_utils.h"
#include "ui/ui.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static const char *TAG = "TFT";

namespace {
constexpr int kW = 240;
constexpr int kH = 320;

// LVGL flush callback (signature must match lv_display_flush_cb_t: uint8_t*)
static void lvglFlushCb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map) {
  esp_lcd_panel_handle_t panel =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
  if (!panel)
    return;

  const int x1 = area->x1, y1 = area->y1;
  const int x2 = area->x2 + 1, y2 = area->y2 + 1;
  const uint32_t px_count = (uint32_t)(x2 - x1) * (y2 - y1);

  lv_draw_sw_rgb565_swap(px_map, px_count);
  esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, px_map);
  lv_display_flush_ready(disp);
}

static uint32_t lastTickMs = 0;

#if (TOUCH_CS_GPIO_NUM >= 0)
// Wrapper with LVGL callback signature so lv_indev_set_read_cb accepts it
static void touchIndevReadCbWrapper(lv_indev_t *indev, lv_indev_data_t *data) {
  touchIndevReadCb(indev, data);
  vTaskDelay(pdMS_TO_TICKS(1)); // yield after touch read so watchdog can be fed
}
#endif
} // namespace

void ESPWiFi::initTFT() {
  if (tftInitialized) {
    return;
  }
  if (!config["tft"]["enabled"].as<bool>()) {
    return;
  }

  // Configure GPIO 12 as input with pull-up (needed for SD card compatibility)
  // Even though we don't use it for TFT MISO, it must be in a known state
  gpio_reset_pin(GPIO_NUM_12);
  gpio_set_direction(GPIO_NUM_12, GPIO_MODE_INPUT);
  gpio_set_pull_mode(GPIO_NUM_12, GPIO_PULLUP_ONLY);
  ESP_LOGI(TAG, "GPIO 12 configured as input with pull-up");

  // Configure backlight GPIO first (but keep it OFF initially)
  if (TFT_BL_GPIO_NUM >= 0) {
    gpio_reset_pin((gpio_num_t)TFT_BL_GPIO_NUM); // Reset pin state first
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TFT_BL_GPIO_NUM);
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)TFT_BL_GPIO_NUM, 0); // OFF initially
    ESP_LOGI(TAG, "Backlight GPIO configured (OFF)");
  }

  // Init SPI bus if not already initialized
  size_t maxTransLen = 0;
  const bool busReady =
      (spi_bus_get_max_transaction_len((spi_host_device_t)TFT_SPI_HOST,
                                       &maxTransLen) == ESP_OK);

  if (!busReady) {
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = TFT_SPI_SCK_GPIO_NUM;
    buscfg.mosi_io_num = TFT_SPI_MOSI_GPIO_NUM;
    buscfg.miso_io_num = -1; // DO NOT USE GPIO 12 - causes SD/Touch conflicts
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = kW * 40 * 2;

    if (spi_bus_initialize((spi_host_device_t)TFT_SPI_HOST, &buscfg,
                           SPI_DMA_CH_AUTO) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to init SPI bus");
      return;
    }
    ESP_LOGI(TAG, "SPI bus initialized");
  } else {
    ESP_LOGI(TAG, "SPI bus already initialized");
  }

  // Panel IO config - let esp_lcd handle the init
  esp_lcd_panel_io_handle_t io_handle = nullptr;
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = TFT_CS_GPIO_NUM;
  io_config.dc_gpio_num = TFT_DC_GPIO_NUM;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40 * 1000 * 1000; // 40MHz (official CYD example)
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;

  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST,
                               &io_config, &io_handle) != ESP_OK) {
    return;
  }

  // Panel driver - let the driver handle init
  esp_lcd_panel_handle_t panel_handle = nullptr;
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = TFT_RST_GPIO_NUM;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;

  if (esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle) !=
      ESP_OK) {
    esp_lcd_panel_io_del(io_handle);
    return;
  }

  // Let the driver do its thing
  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);

  // Set proper orientation for portrait mode (240 wide x 320 tall)
  esp_lcd_panel_swap_xy(panel_handle, true); // Swap XY for portrait

  // Mirror to flip the display for portrait mode
  esp_lcd_panel_mirror(panel_handle, true, true);

  // Set gap to 0 to use full screen
  esp_lcd_panel_set_gap(panel_handle, 0, 0);
  esp_lcd_panel_disp_on_off(panel_handle, true);

  tftSpiBus_ = (void *)TFT_SPI_HOST;
  tftPanelIo_ = (void *)io_handle;
  tftPanel_ = (void *)panel_handle;
  tftInitialized = true;

  ESP_LOGI(TAG, "Panel initialized");

#if (TOUCH_CS_GPIO_NUM >= 0)
  touchBegin();
  if (touchIsActive()) {
    tftTouch_ = (void *)1;
    ESP_LOGI(TAG, "Touch (XPT2046 bitbang) initialized");
  }
#endif

  // Initialize LVGL
  lv_init();

  // Create display
  lv_display_t *disp = lv_display_create(kW, kH);
  lv_display_set_flush_cb(disp, lvglFlushCb);
  lv_display_set_user_data(disp, panel_handle);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  // Allocate draw buffers (1/10 screen size each)
  const size_t buf_size = kW * kH / 10;
  void *buf1 = heap_caps_malloc(buf_size * sizeof(uint16_t), MALLOC_CAP_DMA);
  void *buf2 = heap_caps_malloc(buf_size * sizeof(uint16_t), MALLOC_CAP_DMA);
  lv_display_set_buffers(disp, buf1, buf2, buf_size * sizeof(uint16_t),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  if (tftTouch_) {
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_display(indev, disp);
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchIndevReadCbWrapper);
    lv_indev_set_user_data(indev, tftTouch_);
    ESP_LOGI(TAG, "Touch input device registered");
  }

  ESP_LOGI(TAG, "LVGL initialized");

  // Backlight on before boot animation so video is visible
  if (TFT_BL_GPIO_NUM >= 0) {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TFT_BL_GPIO_NUM);
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)TFT_BL_GPIO_NUM, 1);
    tftBacklightOn_ = true;
    ESP_LOGI(TAG, "Backlight ON");
  }

  // Play boot video (or default splash) before loading main UI so it isn't
  // overwritten
  playBootAnimation();

  // Now load SquareLine UI (File → Export → Export UI Files to src/ui/)
  ui_init();
  registerUiEventHandlers();
}

// LVGL click callback for ui_WiFiButton (user_data = ESPWiFi*)
// static void ui_WiFiButtonClicked(lv_event_t *e) {
//   void *ud = lv_event_get_user_data(e);
//   if (ud) {
//     ESPWiFi *espwifi = static_cast<ESPWiFi *>(ud);
//     espwifi->feedWatchDog();
//     espwifi->updateWiFiInfo("Updating WiFi...");
//     if (ui_WiFiInfoLabel) {
//       lv_obj_invalidate(ui_WiFiInfoLabel);
//     }
//     for (int i = 0; i < 3; i++) {
//       espwifi->renderTFT();
//       espwifi->feedWatchDog(20);
//     }
//     ESP_LOGI(TAG, "WiFi button pressed");
//     espwifi->toggleWiFi();
//     espwifi->feedWatchDog();
//     // set button to checked if WiFi is on (wifi_on icon), unchecked if off
//     if (espwifi->isWiFiInitialized()) {
//       lv_obj_add_state(ui_WiFiButton, LV_STATE_CHECKED);
//       lv_obj_add_state(ui_WiFiSettingsButton, LV_STATE_CHECKED);
//       espwifi->feedWatchDog();
//     } else {
//       lv_obj_remove_state(ui_WiFiButton, LV_STATE_CHECKED);
//       lv_obj_remove_state(ui_WiFiSettingsButton, LV_STATE_CHECKED);
//       espwifi->feedWatchDog();
//     }
//     espwifi->feedWatchDog();
//     espwifi->updateWiFiInfo();
//   }
// }

static void ui_BluetootButtonClicked(lv_event_t *e) {
  ESP_LOGI(TAG, "Bluetooth button pressed");
  void *ud = lv_event_get_user_data(e);
  if (ud) {
    ESPWiFi *espwifi = static_cast<ESPWiFi *>(ud);
    espwifi->feedWatchDog();
#ifdef CONFIG_BT_A2DP_ENABLE
    espwifi->startBluetooth();
    espwifi->feedWatchDog();
    espwifi->startBluetoothScanAsync();
    espwifi->feedWatchDog();
    if (espwifi->isBluetoothScanInProgress())
      espwifi->updateBluetoothInfo("Scanning (9 s)...");
    else
      espwifi->updateBluetoothInfo();
#else
    espwifi->updateBluetoothInfo();
#endif
  }
}

namespace {
static std::vector<std::string> s_bt_dropdown_addresses;
static bool s_bt_has_scanned_at_least_once = false;

static void ui_BluetoothDropdownChanged(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_VALUE_CHANGED)
    return;
  /* Use the dropdown we registered on; event target can be the list when
   * user clicks an option, and get_selected() must be called on the dropdown.
   */
  if (!ui_BluetoothDropdown)
    return;
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(lv_event_get_user_data(e));
  if (!espwifi || s_bt_dropdown_addresses.empty())
    return;
  uint32_t sel = lv_dropdown_get_selected(ui_BluetoothDropdown);
  if (sel >= s_bt_dropdown_addresses.size())
    return;
  const std::string &addr = s_bt_dropdown_addresses[sel];
  if (!addr.empty())
    espwifi->connectBluetooth(addr);
}
} // namespace

void ESPWiFi::registerUiEventHandlers() {
  // if (ui_WiFiButton) {
  //   ESP_LOGI(TAG, "Registering UI event handler for WiFi button");
  //   lv_obj_add_event_cb(ui_WiFiButton, ui_WiFiButtonClicked,
  //   LV_EVENT_CLICKED,
  //                       this);
  // }
  updateWiFiInfo();
  if (ui_BluetoothButton) {
    ESP_LOGI(TAG, "Registering UI event handler for Bluetooth button");
    lv_obj_add_event_cb(ui_BluetoothButton, ui_BluetootButtonClicked,
                        LV_EVENT_CLICKED, this);
    updateBluetoothInfo();
  }
  if (ui_BluetoothDropdown) {
    lv_obj_add_event_cb(ui_BluetoothDropdown, ui_BluetoothDropdownChanged,
                        LV_EVENT_VALUE_CHANGED, this);
  }
}

void ESPWiFi::updateWiFiInfo(std::string info) {
  // if (ui_WiFiInfoLabel) {
  //   if (isWiFiInitialized()) {
  //     feedWatchDog();
  //     // add wifi info to label (combined so all show)
  //     if (info.empty()) {
  //       info = "Hostname:\n    " + getHostname() + "\n" + "\nMode:\n    " +
  //              config["wifi"]["mode"].as<std::string>() + "\n" +
  //              "\nIP Address:\n    " + ipAddress() + "\n" +
  //              "\nMAC Address:\n    " + getMacAddress() + "\n" +
  //              "\nClient SSID:\n    " +
  //              config["wifi"]["client"]["ssid"].as<std::string>() + "\n" +
  //              "\nClient Password:\n    " +
  //              config["wifi"]["client"]["password"].as<std::string>();
  //     }
  //     feedWatchDog();
  //     lv_label_set_text(ui_WiFiInfoLabel, info.c_str());
  //     feedWatchDog();
  //   } else {
  //     feedWatchDog();
  //     if (info.empty()) {
  //       info = "DISABLED";
  //     }
  //     lv_label_set_text(ui_WiFiInfoLabel, info.c_str());
  //     feedWatchDog();
  //   }
  // }
}

void ESPWiFi::updateBluetoothInfo(std::string info) {
  if (!ui_BluetoothInfoLabel) {
    return;
  }
  feedWatchDog();
#ifdef CONFIG_BT_A2DP_ENABLE
  if (!info.empty()) {
    lv_label_set_text(ui_BluetoothInfoLabel, info.c_str());
    if (ui_BluetoothDropdown) {
      lv_dropdown_set_options(ui_BluetoothDropdown, "");
      lv_dropdown_set_text(ui_BluetoothDropdown, "Devices");
      s_bt_dropdown_addresses.clear();
    }
    feedWatchDog();
    return;
  }

  std::string status;
  if (!s_bt_has_scanned_at_least_once) {
    status = "Put device in pairing mode and click the BT to scan";
  } else if (connectBluetoothed) {
    status = "Connected\n";
  } else {
    status = "Select device to connect";
  }
  lv_label_set_text(ui_BluetoothInfoLabel, status.c_str());

  s_bt_dropdown_addresses.clear();
  if (ui_BluetoothDropdown) {
    std::string options;
    size_t n = getDiscoveredDeviceCount();
    /* First option is placeholder so selecting the only device triggers
     * VALUE_CHANGED */
    options = "Select device...";
    s_bt_dropdown_addresses.push_back("");
    for (size_t i = 0; i < n; i++) {
      std::string addr = getDiscoveredDeviceAddress(i);
      if (addr.empty())
        continue;
      std::string name = getDiscoveredDeviceName(i);
      if (name.empty())
        name = addr;
      else
        name = name + " (" + addr + ")";
      options += "\n";
      options += name;
      s_bt_dropdown_addresses.push_back(addr);
    }
    if (s_bt_dropdown_addresses.size() <= 1u) {
      options = "No devices";
      s_bt_dropdown_addresses.clear();
      lv_dropdown_set_options(ui_BluetoothDropdown, options.c_str());
    } else {
      lv_dropdown_set_options(ui_BluetoothDropdown, options.c_str());
      if (connectBluetoothed) {
        const char *addr =
            config["bluetooth"]["last_paired_address"].as<const char *>();
        const char *name =
            config["bluetooth"]["last_paired_name"].as<const char *>();
        const char *display_name = (name && name[0]) ? name : addr;
        if (addr && addr[0]) {
          uint32_t idx = 0;
          for (size_t i = 1; i < s_bt_dropdown_addresses.size(); i++) {
            if (s_bt_dropdown_addresses[i] == addr) {
              idx = (uint32_t)i;
              break;
            }
          }
          if (idx)
            lv_dropdown_set_selected(ui_BluetoothDropdown, idx);
          lv_dropdown_set_text(ui_BluetoothDropdown,
                               display_name ? display_name : "Connected");
        } else {
          lv_dropdown_set_text(ui_BluetoothDropdown, "Connected");
        }
      } else {
        lv_dropdown_set_selected(ui_BluetoothDropdown, 0);
        /* Text shows "Select device..." from option 0 */
      }
    }
  }
#else
  if (info.empty()) {
    info = "Not available\n(Firmware built without A2DP support)";
  }
  lv_label_set_text(ui_BluetoothInfoLabel, info.c_str());
#endif
  feedWatchDog();
}

bool ESPWiFi::playMJPG(const std::string &filepath) {
  if (filepath.empty())
    return false;
  std::string fullPath = resolvePathOnSD(filepath);
  initSDCard();
  FILE *f = fopen(fullPath.c_str(), "rb");
  if (!f) {
    if (sdCard == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(400));
      initSDCard();
      f = fopen(fullPath.c_str(), "rb");
    }
    if (!f) {
      ESP_LOGW(TAG, "playMJPG: cannot open %s (errno=%d)", fullPath.c_str(),
               errno);
      return false;
    }
  }

  esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)tftPanel_;
  if (!panel) {
    fclose(f);
    return false;
  }

  constexpr size_t kJpegMax = 96 * 1024;
  constexpr size_t kRgb565Size = (size_t)kW * kH * 2;
  constexpr size_t kRgb888Size = (size_t)kW * kH * 3;
  uint8_t *jpegBuf = (uint8_t *)heap_caps_malloc(kJpegMax, MALLOC_CAP_INTERNAL);
  uint8_t *rgb888Buf =
      (uint8_t *)heap_caps_malloc(kRgb888Size, MALLOC_CAP_INTERNAL);
  uint8_t *rgbBuf = (uint8_t *)heap_caps_malloc(kRgb565Size, MALLOC_CAP_DMA);
  if (!jpegBuf || !rgb888Buf || !rgbBuf) {
    if (jpegBuf)
      free(jpegBuf);
    if (rgb888Buf)
      free(rgb888Buf);
    if (rgbBuf)
      free(rgbBuf);
    fclose(f);
    return false;
  }

  constexpr size_t kJpegWorkBuf = 3100;
  uint8_t *workBuf =
      (uint8_t *)heap_caps_malloc(kJpegWorkBuf, MALLOC_CAP_INTERNAL);
  if (!workBuf) {
    free(jpegBuf);
    free(rgb888Buf);
    free(rgbBuf);
    fclose(f);
    return false;
  }

  constexpr uint32_t kFrameDelayMs = 50;
  int framesShown = 0;

  while (true) {
    int c = fgetc(f);
    if (c != 0xFF) {
      if (c == EOF)
        break;
      continue;
    }
    if (fgetc(f) != 0xD8)
      continue;
    jpegBuf[0] = 0xFF;
    jpegBuf[1] = 0xD8;
    size_t len = 2;
    while (len < kJpegMax) {
      int b = fgetc(f);
      if (b == EOF)
        break;
      jpegBuf[len++] = (uint8_t)b;
      if (len >= 4 && jpegBuf[len - 2] == 0xFF && jpegBuf[len - 1] == 0xD9)
        break;
    }
    if (len < 4 || jpegBuf[len - 2] != 0xFF || jpegBuf[len - 1] != 0xD9)
      continue;

    esp_jpeg_image_cfg_t jpeg_cfg = {};
    jpeg_cfg.indata = jpegBuf;
    jpeg_cfg.indata_size = len;
    jpeg_cfg.outbuf = rgb888Buf;
    jpeg_cfg.outbuf_size = kRgb888Size;
    jpeg_cfg.out_format = JPEG_IMAGE_FORMAT_RGB888;
    jpeg_cfg.out_scale = JPEG_IMAGE_SCALE_0;
    jpeg_cfg.flags.swap_color_bytes = 0;
    jpeg_cfg.advanced.working_buffer = workBuf;
    jpeg_cfg.advanced.working_buffer_size = kJpegWorkBuf;

    esp_jpeg_image_output_t out_info;
    if (esp_jpeg_decode(&jpeg_cfg, &out_info) != ESP_OK)
      continue;

    uint32_t w = out_info.width;
    uint32_t h = out_info.height;
    if (w > (uint32_t)kW)
      w = kW;
    if (h > (uint32_t)kH)
      h = kH;
    if (w == 0 || h == 0)
      continue;

    const uint32_t npx = w * h;
    const uint8_t *src = rgb888Buf;
    uint16_t *dst = (uint16_t *)rgbBuf;
    for (uint32_t i = 0; i < npx; i++) {
      uint16_t r = src[0] >> 3, g = src[1] >> 2, b = src[2] >> 3;
      *dst++ = (r << 11) | (g << 5) | b;
      src += 3;
    }
    lv_draw_sw_rgb565_swap(rgbBuf, npx);
    esp_lcd_panel_draw_bitmap(panel, 0, 0, (int)w, (int)h, rgbBuf);
    framesShown++;
    vTaskDelay(pdMS_TO_TICKS(kFrameDelayMs));
  }

  free(workBuf);
  free(jpegBuf);
  free(rgb888Buf);
  free(rgbBuf);
  fclose(f);
  return framesShown > 0;
}

void ESPWiFi::playBootAnimation() {
  const char *path = nullptr;
  if (config["tft"]["bootVideo"].is<const char *>())
    path = config["tft"]["bootVideo"].as<const char *>();
  if (!path || path[0] == '\0')
    return;

  ESP_LOGI(TAG, "Boot video: %s", path);
  if (!playMJPG(path)) {
    ESP_LOGW(TAG, "Boot video failed: %s", path);
  }
}

void ESPWiFi::renderTFT() {
  // Handle LVGL tasks (can be heavy; yield so watchdog is fed)
  feedWatchDog();
  if (!tftInitialized) {
    initTFT();
    // Initialize tick counter after init
    if (tftInitialized) {
      lastTickMs = esp_timer_get_time() / 1000;
    }
    return;
  }
  feedWatchDog();
  // Update LVGL tick
  uint32_t nowMs = esp_timer_get_time() / 1000;
  uint32_t elapsed = nowMs - lastTickMs;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    lastTickMs = nowMs;
    feedWatchDog();
  }

#ifdef CONFIG_BT_A2DP_ENABLE
  if (getAndClearBluetoothScanCompleteFlag()) {
    s_bt_has_scanned_at_least_once = true;
    updateBluetoothInfo();
  }
  int btState = getAndClearPendingBluetoothConnectionState();
  if (btState >= 0) {
    connectBluetoothed = (btState == (int)ESP_A2D_CONNECTION_STATE_CONNECTED);
    if (btState == (int)ESP_A2D_CONNECTION_STATE_CONNECTING)
      log(INFO, "🛜 Bluetooth Connecting... 🔄");
    else if (btState == (int)ESP_A2D_CONNECTION_STATE_CONNECTED) {
      log(INFO, "🛜 Bluetooth Connected 🔗");
      const char *attempt_addr =
          config["bluetooth"]["last_connect_attempt_address"]
              .as<const char *>();
      if (attempt_addr && attempt_addr[0]) {
        config["bluetooth"]["last_paired_address"] = attempt_addr;
        const char *attempt_name =
            config["bluetooth"]["last_connect_attempt_name"].as<const char *>();
        config["bluetooth"]["last_paired_name"] =
            attempt_name ? attempt_name : attempt_addr;
        saveConfig();
      }
    } else if (btState == (int)ESP_A2D_CONNECTION_STATE_DISCONNECTED)
      log(INFO, "🛜 Bluetooth Disconnected ⛓️‍💥");
    else if (btState == (int)ESP_A2D_CONNECTION_STATE_DISCONNECTING)
      log(INFO, "🛜 Bluetooth Disconnecting... ⏳");
    updateBluetoothInfo();
  }
#endif
  feedWatchDog();
  lv_timer_handler(); // runs touch + click callbacks + draw
  feedWatchDog();     // yield after so IDLE can run and watchdog is fed
}

#else
void ESPWiFi::initTFT() {}
void ESPWiFi::renderTFT() {}
void ESPWiFi::playBootAnimation() {}
bool ESPWiFi::playMJPG(const std::string &) { return false; }
void ESPWiFi::registerUiEventHandlers() {}
void ESPWiFi::onUiWiFiButtonClicked() {}
void ESPWiFi::updateBluetoothInfo(std::string) {}
#endif
