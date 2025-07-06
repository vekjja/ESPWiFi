#ifndef TOUCH_DISPLAY_H
#define TOUCH_DISPLAY_H

#include "TouchDisplayConfig.h"
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Bitbang.h>

// Touch Display Configuration
#define TOUCH_CS_PIN 5
#define TOUCH_IRQ_PIN 2
#define TFT_CS_PIN 15
#define TFT_DC_PIN 2
#define TFT_RST_PIN 4
#define TFT_BL_PIN 21

// Display dimensions
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240

// Touch calibration values (adjust these based on your specific display)
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3800

// Color definitions
#define BACKGROUND_COLOR TFT_BLACK
#define TEXT_COLOR TFT_WHITE
#define ACCENT_COLOR TFT_BLUE
#define WARNING_COLOR TFT_RED
#define SUCCESS_COLOR TFT_GREEN

// UI Element types
enum UIElementType { BUTTON, LABEL, SLIDER, TOGGLE, PROGRESS_BAR };

// Touch event structure
struct TouchEvent {
  bool touched;
  uint16_t x;
  uint16_t y;
  uint16_t pressure;
};

// UI Element structure
struct UIElement {
  uint16_t id;
  UIElementType type;
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
  String text;
  uint16_t color;
  bool visible;
  bool enabled;
  void (*callback)(uint16_t id, TouchEvent event);
};

class TouchDisplay {
private:
  TFT_eSPI tft;
  XPT2046_Bitbang ts;

  // UI Management
  UIElement *uiElements;
  uint16_t maxElements;
  uint16_t elementCount;

  // Display state
  bool displayInitialized;
  bool backlightOn;
  uint8_t brightness;

  // Touch state
  TouchEvent lastTouchEvent;
  bool touchPressed;
  unsigned long lastTouchTime;

  // Utility functions
  uint16_t mapTouchX(uint16_t rawX);
  uint16_t mapTouchY(uint16_t rawY);
  bool isPointInElement(uint16_t x, uint16_t y, UIElement *element);
  void drawElement(UIElement *element);
  void handleTouchEvent(TouchEvent event);

public:
  // Constructor
  TouchDisplay(uint16_t maxUIElements = 20);
  ~TouchDisplay();

  // Initialization
  bool begin();
  void setBrightness(uint8_t level);
  void setBacklight(bool on);

  // Display functions
  void clear();
  void clear(uint16_t color);
  void drawText(uint16_t x, uint16_t y, const String &text,
                uint16_t color = TEXT_COLOR, uint8_t size = 2);
  void drawTextCentered(uint16_t x, uint16_t y, const String &text,
                        uint16_t color = TEXT_COLOR, uint8_t size = 2);
  void drawRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                uint16_t color);
  void fillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                uint16_t color);
  void drawCircle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color);
  void fillCircle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color);
  void drawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                uint16_t color);

  // Touch functions
  TouchEvent readTouch();
  bool isTouched();
  void calibrateTouch();

  // UI Management
  uint16_t addButton(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     const String &text,
                     void (*callback)(uint16_t id, TouchEvent event) = nullptr);
  uint16_t addLabel(uint16_t x, uint16_t y, const String &text,
                    uint16_t color = TEXT_COLOR);
  uint16_t addSlider(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     void (*callback)(uint16_t id, TouchEvent event) = nullptr);
  uint16_t addToggle(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                     const String &text,
                     void (*callback)(uint16_t id, TouchEvent event) = nullptr);
  uint16_t addProgressBar(uint16_t x, uint16_t y, uint16_t width,
                          uint16_t height, uint8_t progress = 0);

  void removeElement(uint16_t id);
  void showElement(uint16_t id);
  void hideElement(uint16_t id);
  void enableElement(uint16_t id);
  void disableElement(uint16_t id);
  void setElementText(uint16_t id, const String &text);
  void setElementColor(uint16_t id, uint16_t color);
  void setProgress(uint16_t id, uint8_t progress);

  // Screen management
  void drawMainScreen();
  void drawSettingsScreen();
  void drawStatusScreen();
  void updateDisplay();

  // Utility
  void drawWiFiIcon(uint16_t x, uint16_t y, bool connected);
  void drawBatteryIcon(uint16_t x, uint16_t y, uint8_t level);
  void drawSignalIcon(uint16_t x, uint16_t y, int8_t rssi);

  // Loop function
  void update();
};

// Global instance
extern TouchDisplay display;

#endif // TOUCH_DISPLAY_H