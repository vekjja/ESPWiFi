#ifndef LITBOX_LED_MATRIX_H
#define LITBOX_LED_MATRIX_H

#include <driver/rmt.h>
#include <esp_err.h>
#include <cmath>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "LED/Colors.h"
#include "ESPWiFi.h"

#ifndef delay
inline void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
#endif

struct WS2812B {};
struct GRB {};

// LED Matrix Config
#define LED_PIN 2 // Default LED pin for other boards
#define LED_WIDTH 32
#define LED_HEIGHT 8
#define NUM_LEDS (LED_WIDTH * LED_HEIGHT)
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000

inline CRGB leds[NUM_LEDS];
inline uint8_t stripPixels[NUM_LEDS * 3];

// Brightness
inline uint8_t brightness = 9;
inline const uint8_t minBrightness = 1;
inline const uint8_t maxBrightness = 255;

inline rmt_channel_t stripChannel = RMT_CHANNEL_0;
inline bool stripInitialized = false;
inline rmt_item32_t stripItems[NUM_LEDS * 24 + 1];

class FastLEDClass {
public:
  template <typename LED_MODEL, uint8_t PIN, typename COLOR_ORDER>
  void addLeds(CRGB *ledArray, int numLeds) {
    if (!stripInitialized) {
      initializeStrip(PIN, numLeds);
    }
  }

  void setCorrection(const CRGB &) {}
  void setTemperature(const CRGB &) {}

  void setBrightness(uint8_t newBrightness) { brightness = newBrightness; }

  void clear() {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    if (stripInitialized) {
      memset(stripPixels, 0, sizeof(stripPixels));
      transmitPixels();
    }
  }

  void show() {
    if (!stripInitialized) {
      return;
    }
    for (int i = 0; i < NUM_LEDS; i++) {
      CRGB color = leds[i];
      uint8_t r = ((uint32_t)color.r * brightness) / 255;
      uint8_t g = ((uint32_t)color.g * brightness) / 255;
      uint8_t b = ((uint32_t)color.b * brightness) / 255;
      stripPixels[i * 3 + 0] = g;
      stripPixels[i * 3 + 1] = r;
      stripPixels[i * 3 + 2] = b;
    }
    transmitPixels();
  }

private:
  void transmitPixels() {
    const uint32_t T0H = 3;
    const uint32_t T0L = 9;
    const uint32_t T1H = 6;
    const uint32_t T1L = 6;

    int itemCount = 0;
    for (int i = 0; i < NUM_LEDS; i++) {
      uint32_t value = ((uint32_t)stripPixels[i * 3 + 0] << 16) |
                       ((uint32_t)stripPixels[i * 3 + 1] << 8) |
                       stripPixels[i * 3 + 2];
      for (int bit = 23; bit >= 0; --bit) {
        bool one = (value >> bit) & 1;
        stripItems[itemCount].level0 = 1;
        stripItems[itemCount].duration0 = one ? T1H : T0H;
        stripItems[itemCount].level1 = 0;
        stripItems[itemCount].duration1 = one ? T1L : T0L;
        itemCount++;
      }
    }
    stripItems[itemCount].level0 = 0;
    stripItems[itemCount].duration0 = 0;
    stripItems[itemCount].level1 = 0;
    stripItems[itemCount].duration1 = 0;

    esp_err_t err = rmt_write_items(stripChannel, stripItems, itemCount, true);
    if (err == ESP_OK) {
      rmt_wait_tx_done(stripChannel, portMAX_DELAY);
    }
  }

  void initializeStrip(uint8_t pin, int numLeds) {
    rmt_config_t config = {};
    config.rmt_mode = RMT_MODE_TX;
    config.channel = stripChannel;
    config.gpio_num = static_cast<gpio_num_t>(pin);
    config.clk_div = 8;
    config.mem_block_num = 3;
    config.tx_config.loop_en = false;
    config.tx_config.carrier_en = false;
    config.tx_config.idle_output_en = true;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    config.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
      return;
    }

    err = rmt_driver_install(config.channel, 0, 0);
    if (err != ESP_OK) {
      return;
    }

    stripInitialized = true;
  }
};

inline FastLEDClass FastLED;

void ESPWiFi::startLEDMatrix() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();
  log("🌈 LED Matrix Initialized");
}

uint16_t XY(uint8_t x, uint8_t y) {
  y = LED_HEIGHT - 1 - y; // Adjust for orientation
  if (x % 2 == 0) {
    return x * LED_HEIGHT + y; // Even columns top to bottom
  } else {
    return x * LED_HEIGHT + (LED_HEIGHT - 1 - y); // Odd columns bottom to top
  }
}

void drawPixel(uint8_t x, uint8_t y, CRGB color) {
  uint16_t index = XY(x, y);
  if (index < NUM_LEDS) {
    leds[index] = color;
  }
}

void drawCircle(uint8_t x, uint8_t y, uint8_t radius, CRGB color) {
  for (int i = 0; i < LED_WIDTH; i++) {
    for (int j = 0; j < LED_HEIGHT; j++) {
      if (pow(i - x, 2) + pow(j - y, 2) < pow(radius, 2)) {
        drawPixel(i, j, color);
      }
    }
  }
}

void fillMatrix(CRGB color) {
  for (int i = 0; i < LED_WIDTH; i++) {
    for (int j = 0; j < LED_HEIGHT; j++) {
      drawPixel(i, j, color);
    }
  }
}

void testMatrix(CRGB testColor = CRGB::White) {
  for (int x = 0; x < LED_WIDTH; x++) {
    for (int y = 0; y < LED_HEIGHT; y++) {
      drawPixel(x, y, testColor);
      FastLED.show();
      drawPixel(x, y, CRGB::Black);
    }
  }

  int del = 100;
  FastLED.clear();
  drawPixel(0, 0, testColor);
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(0, LED_HEIGHT - 1, testColor);
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(LED_WIDTH - 1, LED_HEIGHT - 1, testColor);
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(LED_WIDTH - 1, 0, testColor);
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(LED_WIDTH / 2, LED_HEIGHT / 2, testColor);
  FastLED.show();
  delay(del);

  FastLED.clear();
  FastLED.show();
}

#endif // LITBOX_LED_MATRIX_H
