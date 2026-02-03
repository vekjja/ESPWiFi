// TFT.cpp - Minimal TFT bring-up (ESP-IDF esp_lcd)
//
// Goal: init once + fill solid RED, nothing else.
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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <algorithm>

#if !defined(TFT_DC_LOW_ON_DATA)
#define TFT_DC_LOW_ON_DATA 0
#endif

namespace {
constexpr int kW = 240;
constexpr int kH = 320;
constexpr int kChunkRows = 40;

SemaphoreHandle_t s_txDone = nullptr;

bool onColorDone(esp_lcd_panel_io_handle_t /*panel_io*/,
                 esp_lcd_panel_io_event_data_t * /*edata*/, void *user_ctx) {
  BaseType_t high_task_wakeup = pdFALSE;
  if (user_ctx) {
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &high_task_wakeup);
  }
  return high_task_wakeup == pdTRUE;
}

void fillSolidRed(esp_lcd_panel_handle_t panel) {
  if (!panel) {
    return;
  }

  if (!s_txDone) {
    // Should never happen once IO is created, but keep it safe.
    return;
  }

  const size_t pixels = (size_t)kW * (size_t)kChunkRows;
  const size_t bytes = pixels * 2;
  uint8_t *buf = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
  if (!buf) {
    return;
  }

  // RGB565 red = 0xF800 (MSB first).
  for (size_t i = 0; i < pixels; ++i) {
    buf[i * 2 + 0] = 0xF8;
    buf[i * 2 + 1] = 0x00;
  }

  for (int y = 0; y < kH; y += kChunkRows) {
    const int rows = std::min(kChunkRows, kH - y);
    (void)xSemaphoreTake(s_txDone, 0);
    (void)esp_lcd_panel_draw_bitmap(panel, 0, y, kW, y + rows, buf);
    (void)xSemaphoreTake(s_txDone, pdMS_TO_TICKS(1000));
  }

  heap_caps_free(buf);
}
} // namespace

void ESPWiFi::initTFT() {
  if (tftInitialized) {
    return;
  }
  if (!config["tft"]["enabled"].as<bool>()) {
    return;
  }

  // Key pins (expected):
  // BL=21, MISO=12, MOSI=13, SCLK=14, CS=15, DC=2, RST=-1
  if (TFT_SPI_SCK_GPIO_NUM < 0 || TFT_SPI_MOSI_GPIO_NUM < 0 ||
      TFT_CS_GPIO_NUM < 0 || TFT_DC_GPIO_NUM < 0) {
    return;
  }

  // Backlight ON
  if (TFT_BL_GPIO_NUM >= 0) {
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TFT_BL_GPIO_NUM);
    (void)gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)TFT_BL_GPIO_NUM, 1);
    tftBacklightOn_ = true;
  }

  // Init SPI bus if needed (shared-bus safe)
  size_t maxTransLen = 0;
  const bool busReady =
      (spi_bus_get_max_transaction_len((spi_host_device_t)TFT_SPI_HOST,
                                       &maxTransLen) == ESP_OK);
  if (!busReady) {
    spi_bus_config_t buscfg = {};
    buscfg.sclk_io_num = TFT_SPI_SCK_GPIO_NUM;
    buscfg.mosi_io_num = TFT_SPI_MOSI_GPIO_NUM;
    buscfg.miso_io_num = TFT_SPI_MISO_GPIO_NUM;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = kW * kChunkRows * 2;
    if (spi_bus_initialize((spi_host_device_t)TFT_SPI_HOST, &buscfg,
                           SPI_DMA_CH_AUTO) != ESP_OK) {
      return;
    }
  }

  // Panel IO
  esp_lcd_panel_io_handle_t io_handle = nullptr;
  esp_lcd_panel_handle_t panel_handle = nullptr;

  if (!s_txDone) {
    s_txDone = xSemaphoreCreateBinary();
  }

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = TFT_CS_GPIO_NUM;
  io_config.dc_gpio_num = TFT_DC_GPIO_NUM;
  io_config.spi_mode = TFT_SPI_MODE;
  io_config.pclk_hz = TFT_PCLK_HZ;
  io_config.trans_queue_depth = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.on_color_trans_done = onColorDone;
  io_config.user_ctx = s_txDone;
  io_config.flags.dc_low_on_data = TFT_DC_LOW_ON_DATA ? 1 : 0;

  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST,
                               &io_config, &io_handle) != ESP_OK) {
    return;
  }

  // Panel driver (minimal; no probing)
  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = TFT_RST_GPIO_NUM;
  panel_config.color_space = ESP_LCD_COLOR_SPACE_RGB;
  panel_config.bits_per_pixel = TFT_BITS_PER_PIXEL;

  if (esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle) !=
      ESP_OK) {
    (void)esp_lcd_panel_io_del(io_handle);
    return;
  }

  (void)esp_lcd_panel_reset(panel_handle);
  (void)esp_lcd_panel_init(panel_handle);
  (void)esp_lcd_panel_disp_on_off(panel_handle, true);

  tftSpiBus_ = (void *)TFT_SPI_HOST;
  tftPanelIo_ = (void *)io_handle;
  tftPanel_ = (void *)panel_handle;
  tftTouch_ = nullptr;
  tftInitialized = true;

  // One-shot RED fill.
  fillSolidRed(panel_handle);
}

void ESPWiFi::runTFT() {
  if (!tftInitialized) {
    initTFT();
  }
}

#else
void ESPWiFi::initTFT() {}
void ESPWiFi::runTFT() {}
#endif
