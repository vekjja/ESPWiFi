// Touch.cpp - XPT2046 software (bitbang) SPI for CYD

#include "Touch.h"

#if ESPWiFi_HAS_TFT && (TOUCH_CS_GPIO_NUM >= 0)

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include <cstdint>

static const char *TAG = "TFT";

namespace {
constexpr int kW = 240;
constexpr int kH = 320;

static constexpr uint8_t kCmdReadX = 0x91;
static constexpr uint8_t kCmdReadY = 0xD1;
static constexpr uint8_t kCmdReadZ1 = 0xB1;
static constexpr uint8_t kCmdReadZ2 = 0xC1;
static constexpr int kBitbangDelayUs = 5;
static constexpr uint16_t kTouchPressureThreshold = 400;
static constexpr unsigned kTouchDebounceCount = 2;

static inline void bitbangDelay() { esp_rom_delay_us(kBitbangDelayUs); }

static void xpt2046BitbangWrite(uint8_t cmd) {
  for (int i = 7; i >= 0; i--) {
    gpio_set_level((gpio_num_t)TOUCH_SPI_MOSI_GPIO_NUM, (cmd >> i) & 1);
    gpio_set_level((gpio_num_t)TOUCH_SPI_SCK_GPIO_NUM, 0);
    bitbangDelay();
    gpio_set_level((gpio_num_t)TOUCH_SPI_SCK_GPIO_NUM, 1);
    bitbangDelay();
  }
  gpio_set_level((gpio_num_t)TOUCH_SPI_MOSI_GPIO_NUM, 0);
  gpio_set_level((gpio_num_t)TOUCH_SPI_SCK_GPIO_NUM, 0);
}

static uint16_t xpt2046BitbangRead(uint8_t cmd) {
  xpt2046BitbangWrite(cmd);
  uint16_t result = 0;
  for (int i = 15; i >= 0; i--) {
    gpio_set_level((gpio_num_t)TOUCH_SPI_SCK_GPIO_NUM, 1);
    bitbangDelay();
    gpio_set_level((gpio_num_t)TOUCH_SPI_SCK_GPIO_NUM, 0);
    bitbangDelay();
    result |= (uint16_t)gpio_get_level((gpio_num_t)TOUCH_SPI_MISO_GPIO_NUM)
              << i;
  }
  return result >> 4;
}

struct TouchPoint {
  int32_t x;
  int32_t y;
  bool pressed;
};

struct TouchRaw {
  uint16_t xRaw;
  uint16_t yRaw;
  bool pressed;
};

static TouchRaw getTouchRaw() {
  TouchRaw out = {0, 0, false};
  gpio_set_level((gpio_num_t)TOUCH_CS_GPIO_NUM, 0);
  uint16_t z1 = xpt2046BitbangRead(kCmdReadZ1);
  uint16_t z2 = xpt2046BitbangRead(kCmdReadZ2);
  uint16_t z = z1 + 4095 - z2;
  if (z < kTouchPressureThreshold) {
    gpio_set_level((gpio_num_t)TOUCH_CS_GPIO_NUM, 1);
    return out;
  }
  out.xRaw = xpt2046BitbangRead(kCmdReadX);
  out.yRaw = xpt2046BitbangRead(kCmdReadY & 0xFE);
  gpio_set_level((gpio_num_t)TOUCH_CS_GPIO_NUM, 1);
  out.pressed = true;
  return out;
}

static TouchPoint getTouch() {
  TouchPoint out = {0, 0, false};
  TouchRaw raw = getTouchRaw();
  if (!raw.pressed)
    return out;

  // Map raw 0..4095 to display 0..(kW-1), 0..(kH-1). Mirror X and Y so (0,0) =
  // top-left.
  int32_t tx = (int32_t)raw.xRaw * (kW - 1) / 4095;
  int32_t ty = (int32_t)raw.yRaw * (kH - 1) / 4095;
  int32_t dx = (kW - 1) - tx;
  int32_t dy = (kH - 1) - ty;
  if (dx < 0)
    dx = 0;
  if (dx >= kW)
    dx = kW - 1;
  if (dy < 0)
    dy = 0;
  if (dy >= kH)
    dy = kH - 1;
  out.x = dx;
  out.y = dy;
  out.pressed = true;
  return out;
}

static bool s_touchInited = false;

static unsigned s_consecutivePressed = 0;
static int32_t s_lastX = 0, s_lastY = 0;
} // namespace

void touchBegin() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << TOUCH_SPI_MOSI_GPIO_NUM) |
                    (1ULL << TOUCH_SPI_SCK_GPIO_NUM) |
                    (1ULL << TOUCH_CS_GPIO_NUM);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  io.pin_bit_mask = (1ULL << TOUCH_SPI_MISO_GPIO_NUM);
  io.mode = GPIO_MODE_INPUT;
  gpio_config(&io);
  gpio_set_level((gpio_num_t)TOUCH_CS_GPIO_NUM, 1);
  gpio_set_level((gpio_num_t)TOUCH_SPI_SCK_GPIO_NUM, 0);
  s_touchInited = true;

  gpio_set_level((gpio_num_t)TOUCH_CS_GPIO_NUM, 0);
  uint16_t z1 = xpt2046BitbangRead(kCmdReadZ1);
  uint16_t z2 = xpt2046BitbangRead(kCmdReadZ2);
  uint16_t z = z1 + 4095 - z2;
  uint16_t xR = xpt2046BitbangRead(kCmdReadX);
  uint16_t yR = xpt2046BitbangRead(kCmdReadY & 0xFE);
  gpio_set_level((gpio_num_t)TOUCH_CS_GPIO_NUM, 1);
  ESP_LOGI(
      TAG,
      "Touch init: z1=%u z2=%u z=%u xRaw=%u yRaw=%u (no touch; threshold=%u)",
      (unsigned)z1, (unsigned)z2, (unsigned)z, (unsigned)xR, (unsigned)yR,
      (unsigned)kTouchPressureThreshold);
}

void touchIndevReadCb(void *indev, void *data) {
  (void)indev;
  lv_indev_data_t *d = (lv_indev_data_t *)data;
  if (!s_touchInited) {
    d->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  TouchPoint p = getTouch();
  if (p.pressed) {
    s_lastX = p.x;
    s_lastY = p.y;
    if (s_consecutivePressed < 255)
      s_consecutivePressed++;
    if (s_consecutivePressed >= kTouchDebounceCount) {
      d->point.x = p.x;
      d->point.y = p.y;
      d->state = LV_INDEV_STATE_PRESSED;
      ESP_LOGI(TAG, "Touch (%ld, %ld)", (long)p.x, (long)p.y);
    } else {
      d->point.x = s_lastX;
      d->point.y = s_lastY;
      d->state = LV_INDEV_STATE_RELEASED;
    }
  } else {
    s_consecutivePressed = 0;
    d->point.x = s_lastX;
    d->point.y = s_lastY;
    d->state = LV_INDEV_STATE_RELEASED;
  }
}

bool touchIsActive() { return s_touchInited; }

#endif // ESPWiFi_HAS_TFT && (TOUCH_CS_GPIO_NUM >= 0)
