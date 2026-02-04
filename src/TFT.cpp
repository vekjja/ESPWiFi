// TFT.cpp - LVGL display with esp_lcd backend
#include "ESPWiFi.h"

#if ESPWiFi_HAS_TFT

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "TFT";

namespace {
constexpr int kW = 240;
constexpr int kH = 320;

// LVGL flush callback
static void lvglFlushCb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map) {
  esp_lcd_panel_handle_t panel =
      (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
  if (!panel)
    return;

  const int x1 = area->x1;
  const int y1 = area->y1;
  const int x2 = area->x2 + 1;
  const int y2 = area->y2 + 1;

  esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, px_map);
  lv_display_flush_ready(disp);

  static int flushCount = 0;
  if (flushCount < 5) {
    ESP_LOGI(TAG, "LVGL flush #%d: (%d,%d)->(%d,%d)", ++flushCount, x1, y1, x2,
             y2);
  }
}

static uint32_t lastTickMs = 0;
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
  panel_config.rgb_ele_order =
      LCD_RGB_ELEMENT_ORDER_BGR; // ILI9341 on ESP32-2432S028R uses BGR (per
                                 // official example)
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
  // esp_lcd_panel_mirror(panel_handle, false, true); // Mirror Y only for
  // portrait Set gap to 0 to use full screen
  esp_lcd_panel_set_gap(panel_handle, 0, 0);
  esp_lcd_panel_disp_on_off(panel_handle, true);

  tftSpiBus_ = (void *)TFT_SPI_HOST;
  tftPanelIo_ = (void *)io_handle;
  tftPanel_ = (void *)panel_handle;
  tftInitialized = true;

  ESP_LOGI(TAG, "Panel initialized");

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

  // Draw "Hello world" label
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x003a57),
                            LV_PART_MAIN);
  lv_obj_t *label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "Hello world");
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

  ESP_LOGI(TAG, "LVGL initialized");

  // NOW turn on backlight - display is fully configured
  if (TFT_BL_GPIO_NUM >= 0) {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TFT_BL_GPIO_NUM);
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)TFT_BL_GPIO_NUM, 1);
    tftBacklightOn_ = true;
    ESP_LOGI(TAG, "Backlight ON");
  }
}

void ESPWiFi::runTFT() {
  if (!tftInitialized) {
    initTFT();
    // Initialize tick counter after init
    if (tftInitialized) {
      lastTickMs = esp_timer_get_time() / 1000;
    }
    return;
  }

  // Update LVGL tick
  uint32_t nowMs = esp_timer_get_time() / 1000;
  uint32_t elapsed = nowMs - lastTickMs;
  if (elapsed > 0) {
    lv_tick_inc(elapsed);
    lastTickMs = nowMs;
  }

  // Handle LVGL tasks
  lv_timer_handler();
}

#else
void ESPWiFi::initTFT() {}
void ESPWiFi::runTFT() {}
#endif
