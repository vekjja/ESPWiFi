#ifndef LITBOX_COLORS_H
#define LITBOX_COLORS_H

#include <FastLED.h>

struct Pixel {
  float x = 0, y = 0;
  float vx = 0, vy = 0;
  CRGB color = CRGB(0, 0, 0);
  int colorPaletteIndex = 0;
};

// Palette and Configurations
const int palletSize = 4;
CRGB pixelColor = CRGB(255, 255, 255); // Default color
CRGB pixelBgColor = CRGB(0, 0, 0);     // Default background color
CRGB colorPallet[palletSize] = {CRGB(0, 0, 255), CRGB(0, 255, 255),
                                CRGB(148, 0, 211), CRGB(255, 255, 255)};

// Utility: Parse a hex color string (e.g., "#RRGGBB" or "RRGGBB") to CRGB
inline CRGB hexToCRGB(const String &hex) {
  String hexStr = hex;
  if (hexStr.startsWith("#"))
    hexStr = hexStr.substring(1);
  if (hexStr.length() != 6)
    return CRGB(0, 0, 0); // Invalid, return black
  long number = strtol(hexStr.c_str(), nullptr, 16);
  uint8_t r = (number >> 16) & 0xFF;
  uint8_t g = (number >> 8) & 0xFF;
  uint8_t b = number & 0xFF;
  return CRGB(r, g, b);
}

// Function to convert CRGB to 16-bit color
uint16_t crgbTo16bit(CRGB color) {
  uint8_t r = color.r >> 3; // 5 bits
  uint8_t g = color.g >> 2; // 6 bits
  uint8_t b = color.b >> 3; // 5 bits
  return (r << 11) | (g << 5) | b;
}

#endif // LITBOX_COLORS_H
