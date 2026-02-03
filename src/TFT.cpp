// TFT.cpp - Optional TFT + touch UI for supported boards (ESP-IDF esp_lcd)
//
// Target board: ESP32-2432S028R (often ST7789 + XPT2046 touch)
//
#include "ESPWiFi.h"

#if ESPWiFi_HAS_TFT

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include <algorithm>

#if !defined(TFT_BITS_PER_PIXEL)
#define TFT_BITS_PER_PIXEL 16
#endif
#if !defined(TFT_PCLK_HZ)
#define TFT_PCLK_HZ 10000000
#endif
#if !defined(TFT_SPI_MODE)
#define TFT_SPI_MODE 0
#endif

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

static void fillRectSolidRed(esp_lcd_panel_handle_t panel, int screenW,
                             int screenH, const Rect &r) {
  if (!panel || r.w <= 0 || r.h <= 0)
    return;
  int x2 = std::min(r.x + r.w, screenW);
  int y2 = std::min(r.y + r.h, screenH);
  int x1 = std::max(r.x, 0);
  int y1 = std::max(r.y, 0);
  if (x2 <= x1 || y2 <= y1)
    return;

  const int w = x2 - x1;
  constexpr int kChunkRows = 40;
  static uint8_t *dmaBuf = nullptr;
  static int dmaBufW = 0;
  static int dmaBufRows = 0;

  if (!dmaBuf || dmaBufW < w || dmaBufRows < kChunkRows) {
    if (dmaBuf) {
      heap_caps_free(dmaBuf);
      dmaBuf = nullptr;
    }
    dmaBufW = w;
    dmaBufRows = kChunkRows;
    dmaBuf = (uint8_t *)heap_caps_malloc(
        (size_t)dmaBufW * (size_t)dmaBufRows * 2, MALLOC_CAP_DMA);
  }

  if (!dmaBuf) {
    return;
  }

  // RGB565 red = 0xF800, send MSB first: F8 00
  for (int i = 0; i < (dmaBufW * dmaBufRows); ++i) {
    dmaBuf[(size_t)i * 2 + 0] = 0xF8;
    dmaBuf[(size_t)i * 2 + 1] = 0x00;
  }

  for (int y = y1; y < y2; y += kChunkRows) {
    int rows = std::min(kChunkRows, y2 - y);
    esp_lcd_panel_draw_bitmap(panel, x1, y, x2, y + rows, dmaBuf);
  }
}

void ESPWiFi::initTFT() {
  if (tftInitialized) {
    return;
  }

  // Allow disabling via config even when compiled in.
  if (!config["tft"]["enabled"].as<bool>()) {
    return;
  }

  if (TFT_SPI_SCK_GPIO_NUM < 0 || TFT_SPI_MOSI_GPIO_NUM < 0 ||
      TFT_CS_GPIO_NUM < 0 || TFT_DC_GPIO_NUM < 0) {
    log(WARNING, "🖥️ TFT pins not set; skipping TFT init");
    return;
  }

  // Backlight
  if (TFT_BL_GPIO_NUM >= 0) {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TFT_BL_GPIO_NUM);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    (void)gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)TFT_BL_GPIO_NUM, 1);
    tftBacklightOn_ = true;
  }

  // Manual reset pulse: CYD/ESP32-2432S028R boards have multiple revisions in
  // the wild (reset pin is commonly GPIO4). Keep this list minimal because
  // some revisions wire GPIO12 to TFT MISO.
  auto pulseResetPin = [](int gpioNum) {
    if (gpioNum < 0) {
      return;
    }
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << gpioNum);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    (void)gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)gpioNum, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)gpioNum, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
  };

  pulseResetPin(TFT_RST_CANDIDATE0_GPIO_NUM);
  pulseResetPin(TFT_RST_CANDIDATE1_GPIO_NUM);

  // SPI bus for TFT (+ touch)
  esp_err_t err = ESP_OK;
  // Avoid triggering ESP-IDF's "SPI bus already initialized" error log:
  // probe bus state first.
  size_t maxTransLen = 0;
  bool busReady =
      (spi_bus_get_max_transaction_len((spi_host_device_t)TFT_SPI_HOST,
                                       &maxTransLen) == ESP_OK);
  if (!busReady) {
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = TFT_SPI_SCK_GPIO_NUM;
    buscfg.mosi_io_num = TFT_SPI_MOSI_GPIO_NUM;
    buscfg.miso_io_num = TFT_SPI_MISO_GPIO_NUM;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 320 * 40 * 2; // RGB565

    err = spi_bus_initialize((spi_host_device_t)TFT_SPI_HOST, &buscfg,
                             SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
      log(ERROR, "🖥️ TFT spi_bus_initialize failed: %s", esp_err_to_name(err));
      return;
    }
  }

  // Panel IO
  esp_lcd_panel_io_handle_t io_handle = nullptr;
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = TFT_CS_GPIO_NUM;
  io_config.dc_gpio_num = TFT_DC_GPIO_NUM;
  io_config.spi_mode = TFT_SPI_MODE;
  io_config.pclk_hz = TFT_PCLK_HZ;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.on_color_trans_done = nullptr;
  io_config.user_ctx = nullptr;

  err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST,
                                 &io_config, &io_handle);
  if (err != ESP_OK) {
    log(ERROR, "🖥️ TFT esp_lcd_new_panel_io_spi failed: %s",
        esp_err_to_name(err));
    return;
  }

  // Panel driver (ST7789)
  esp_lcd_panel_handle_t panel_handle = nullptr;
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = TFT_RST_GPIO_NUM;
  // Your current symptom (solid blue when trying to draw red) indicates the
  // panel's RGB/BGR order is swapped. Use RGB here so red stays red.
  panel_config.color_space = ESP_LCD_COLOR_SPACE_RGB;
  panel_config.bits_per_pixel = TFT_BITS_PER_PIXEL;

  err = esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);
  if (err != ESP_OK) {
    log(ERROR, "🖥️ TFT esp_lcd_new_panel_st7789 failed: %s",
        esp_err_to_name(err));
    return;
  }

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  // Use portrait coordinates (240x320). Many boards need mirroring; we keep
  // defaults conservative and can tune if needed.
  (void)esp_lcd_panel_swap_xy(panel_handle, false);
  (void)esp_lcd_panel_mirror(panel_handle, false, true);
  // Some panels need inversion to look correct; it doesn't hurt bring-up.
  (void)esp_lcd_panel_invert_color(panel_handle, false);

  tftSpiBus_ = (void *)TFT_SPI_HOST;
  tftPanelIo_ = (void *)io_handle;
  tftPanel_ = (void *)panel_handle;
  tftTouch_ = nullptr; // bring-up: keep display-only until panel is stable
  tftInitialized = true;

  log(INFO, "🖥️ TFT initialized (ST7789)");
}

void ESPWiFi::runTFT() {
  if (!config["tft"]["enabled"].as<bool>()) {
    return;
  }
  if (!tftInitialized) {
    initTFT();
  }
  if (!tftInitialized) {
    return;
  }

  auto *panel = (esp_lcd_panel_handle_t)tftPanel_;

  constexpr int kW = 240;
  constexpr int kH = 320;

  // Draw once: full-screen red.
  static bool drew = false;
  if (drew) {

    return;
  }
  fillRectSolidRed(panel, kW, kH, Rect{0, 0, kW, kH});
  drew = true;
}

#else

// Stubs when TFT is not compiled in
void ESPWiFi::initTFT() {}
void ESPWiFi::runTFT() {}

#endif
