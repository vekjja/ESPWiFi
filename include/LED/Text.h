#ifndef TEXT_H
#define TEXT_H

#include "Colors.h"
#include "LEDMatrix.h"

// Global matrix instance
FastLED_NeoMatrix matrix(leds, LED_WIDTH, LED_HEIGHT);

const int maxLen = 200;

// Text configuration
char textContent[maxLen] = "*.*. Lit Box .*.*"; // 40 chars max + null
char textAnimation[16] = "scroll";
bool textRequested = false;
int textSpeed = 75;
int textSize = 1;

void scrollText(const char *text) {

  matrix.setTextSize(textSize);
  matrix.setTextColor(crgbTo16bit(pixelColor));

  const int charWidth = 6;
  int textPixelWidth = strlen(text) * charWidth;
  int scrollEnd = textPixelWidth + 1;

  for (int x = 0; x < scrollEnd; x++) {
    fill_solid(leds, LED_WIDTH * LED_HEIGHT, pixelBgColor);
    matrix.setCursor(-x, 0);
    matrix.print(text);
    matrix.show();
    delay(constrain(120 - textSpeed, 20, 200));
    yield();
  }
}

void staticText(const char *text) {
  if (!text || strlen(text) == 0) {
    return;
  }
  matrix.setTextSize(textSize);
  matrix.setTextColor(crgbTo16bit(pixelColor));
  int textLength = strlen(text) * 6;
  int xStart = (LED_WIDTH - textLength) / 2;
  int yStart = (LED_HEIGHT - 8) / 2;
  FastLED.clear();
  fill_solid(leds, LED_WIDTH * LED_HEIGHT, pixelBgColor);
  matrix.setCursor(xStart, yStart);
  matrix.print(text);
  FastLED.show();
}

void waveText(const char *text) {
  if (!text || strlen(text) == 0) {
    return;
  }
  matrix.setTextSize(textSize);
  matrix.setTextColor(crgbTo16bit(pixelColor));
  int textPixelSize = 4;
  int textLength = strlen(textContent) * textPixelSize;
  for (int x = 0; x < LED_WIDTH + textLength; x++) {
    matrix.clear();
    for (int i = 0; i < LED_WIDTH * LED_HEIGHT; i++) {
      leds[i] = pixelBgColor;
    }
    int y = sin(x / 2.0) * 2;
    y = constrain(y, 0, LED_HEIGHT - 8);
    matrix.setCursor(LED_WIDTH - x, y);
    matrix.print(textContent);
    matrix.show();
    delay(100 - textSpeed);
  }
}

bool textFits(const char *text) {
  if (!text || strlen(text) == 0) {
    return false;
  }
  return (strlen(text) * 6) <= LED_WIDTH;
}

void displayOrScrollText(const char *text) {
  if (!text || strlen(text) == 0) {
    return;
  }
  if (strlen(text) > 20) {
    scrollText(text);
  } else if (textFits(text)) {
    staticText(text);
  } else {
    scrollText(text);
  }
}

#endif