#ifndef LITBOX_LED_MATRIX_H
#define LITBOX_LED_MATRIX_H

#include <Adafruit_GFX.h>
#include <FastLED.h>

// FastLED configuration for ESP32-S3
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define FASTLED_ESP32_I2S 1
#define FASTLED_RMT_CHANNEL 1
#define FASTLED_RMT5_RECYCLE 1
#else
// For other ESP32 variants
#define FASTLED_RMT_CHANNEL 1
#endif

#define FASTLED_SHOW_CORE 0

#include "ESPWiFi.h"

// Custom FastLED NeoMatrix class for our LED matrix
class FastLED_NeoMatrix : public Adafruit_GFX {
private:
  CRGB *leds;
  uint16_t numLeds;
  uint8_t width;
  uint8_t height;

  uint16_t XY(uint8_t x, uint8_t y) {
    if (x % 2 == 0) {
      return x * height + y; // Even columns top to bottom
    } else {
      return x * height + (height - 1 - y); // Odd columns bottom to top
    }
  }

public:
  FastLED_NeoMatrix(CRGB *ledArray, uint8_t w, uint8_t h)
      : Adafruit_GFX(w, h), leds(ledArray), width(w), height(h) {
    numLeds = w * h;
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= width || y < 0 || y >= height)
      return;

    uint16_t index = XY(x, y);
    if (index < numLeds) {
      // Convert 16-bit color to CRGB
      uint8_t r = (color >> 11) << 3;         // 5 bits red
      uint8_t g = ((color >> 5) & 0x3F) << 2; // 6 bits green
      uint8_t b = (color & 0x1F) << 3;        // 5 bits blue
      leds[index] = CRGB(r, g, b);
    }
  }

  void drawPixel(int16_t x, int16_t y, CRGB color) {
    if (x < 0 || x >= width || y < 0 || y >= height)
      return;

    uint16_t index = XY(x, y);
    if (index < numLeds) {
      leds[index] = color;
    }
  }

  void clear() { FastLED.clear(); }

  void show() { FastLED.show(); }
};

// LED Matrix Config
#define LED_PIN 2 // Default LED pin for other boards
#define LED_WIDTH 32
#define LED_HEIGHT 8
#define NUM_LEDS (LED_WIDTH * LED_HEIGHT)

CRGB leds[NUM_LEDS];

// Brightness
uint8_t brightness = 9;
const uint8_t minBrightness = 1;
const uint8_t maxBrightness = 255;

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
  // Light up all LEDs one by one
  for (int x = 0; x < LED_WIDTH; x++) {
    for (int y = 0; y < LED_HEIGHT; y++) {
      drawPixel(x, y, testColor);
      FastLED.show();
      // delay(10);
      drawPixel(x, y, CRGB::Black);
    }
  }

  int del = 100;
  // Test specific positions
  FastLED.clear();
  drawPixel(0, 0, testColor); // Bottom left
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(0, LED_HEIGHT - 1, testColor); // Top left
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(LED_WIDTH - 1, LED_HEIGHT - 1, testColor); // Top right
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(LED_WIDTH - 1, 0, testColor); // Bottom right
  FastLED.show();
  delay(del);

  FastLED.clear();
  drawPixel(LED_WIDTH / 2, LED_HEIGHT / 2, testColor); // Center
  FastLED.show();
  delay(del);

  FastLED.clear();
  FastLED.show();
}

#endif // LITBOX_LED_MATRIX_H