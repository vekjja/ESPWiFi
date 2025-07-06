#include <TouchDisplay.h>

// Global instance
TouchDisplay display;

// Constructor
TouchDisplay::TouchDisplay(uint16_t maxUIElements)
    : ts(TOUCH_CS_PIN, TOUCH_IRQ_PIN, TOUCH_IRQ_PIN, TOUCH_CS_PIN,
         DISPLAY_WIDTH, DISPLAY_HEIGHT) {
  maxElements = maxUIElements;
  uiElements = new UIElement[maxElements];
  elementCount = 0;
  displayInitialized = false;
  backlightOn = true;
  brightness = 255;
  touchPressed = false;
  lastTouchTime = 0;

  // Initialize touch event
  lastTouchEvent.touched = false;
  lastTouchEvent.x = 0;
  lastTouchEvent.y = 0;
  lastTouchEvent.pressure = 0;
}

// Destructor
TouchDisplay::~TouchDisplay() {
  if (uiElements) {
    delete[] uiElements;
  }
}

// Initialize the display
bool TouchDisplay::begin() {
  // Initialize TFT display
  tft.begin();
  tft.setRotation(1); // Landscape orientation
  tft.fillScreen(BACKGROUND_COLOR);

  // Initialize touch screen
  ts.begin();

  // Set up backlight pin
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  displayInitialized = true;

  // Draw initial screen
  clear();
  drawMainScreen();

  return true;
}

// Set display brightness
void TouchDisplay::setBrightness(uint8_t level) {
  brightness = level;
  analogWrite(TFT_BL_PIN, brightness);
}

// Control backlight
void TouchDisplay::setBacklight(bool on) {
  backlightOn = on;
  if (on) {
    analogWrite(TFT_BL_PIN, brightness);
  } else {
    digitalWrite(TFT_BL_PIN, LOW);
  }
}

// Clear display
void TouchDisplay::clear() { clear(BACKGROUND_COLOR); }

void TouchDisplay::clear(uint16_t color) { tft.fillScreen(color); }

// Text drawing functions
void TouchDisplay::drawText(uint16_t x, uint16_t y, const String &text,
                            uint16_t color, uint8_t size) {
  tft.setTextColor(color);
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.print(text);
}

void TouchDisplay::drawTextCentered(uint16_t x, uint16_t y, const String &text,
                                    uint16_t color, uint8_t size) {
  tft.setTextColor(color);
  tft.setTextSize(size);
  int16_t textWidth = tft.textWidth(text);
  int16_t textHeight = size * 8;
  tft.setCursor(x - textWidth / 2, y - textHeight / 2);
  tft.print(text);
}

// Drawing functions
void TouchDisplay::drawRect(uint16_t x, uint16_t y, uint16_t width,
                            uint16_t height, uint16_t color) {
  tft.drawRect(x, y, width, height, color);
}

void TouchDisplay::fillRect(uint16_t x, uint16_t y, uint16_t width,
                            uint16_t height, uint16_t color) {
  tft.fillRect(x, y, width, height, color);
}

void TouchDisplay::drawCircle(uint16_t x, uint16_t y, uint16_t radius,
                              uint16_t color) {
  tft.drawCircle(x, y, radius, color);
}

void TouchDisplay::fillCircle(uint16_t x, uint16_t y, uint16_t radius,
                              uint16_t color) {
  tft.fillCircle(x, y, radius, color);
}

void TouchDisplay::drawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
                            uint16_t color) {
  tft.drawLine(x1, y1, x2, y2, color);
}

// Touch functions
TouchEvent TouchDisplay::readTouch() {
  TouchEvent event;
  event.touched = false;

  TouchPoint p = ts.getTouch();
  if (p.zRaw > 100) { // Threshold for touch detection
    event.touched = true;
    event.x = p.x;
    event.y = p.y;
    event.pressure = p.zRaw;
  }

  return event;
}

bool TouchDisplay::isTouched() {
  TouchPoint p = ts.getTouch();
  return p.zRaw > 100;
}

// Touch calibration
void TouchDisplay::calibrateTouch() {
  // This is a basic calibration - you may need to adjust the mapping values
  // based on your specific display's touch characteristics
  clear();
  drawTextCentered(DISPLAY_WIDTH / 2, 50, "Touch Calibration", TEXT_COLOR, 3);
  drawTextCentered(DISPLAY_WIDTH / 2, 100, "Touch the corners", TEXT_COLOR, 2);

  // Wait for touch and read values
  while (!isTouched()) {
    delay(10);
  }

  TouchPoint p = ts.getTouch();
  // You can store these values and use them for better calibration
  // For now, we'll use the default values defined in the header
}

// Touch coordinate mapping
uint16_t TouchDisplay::mapTouchX(uint16_t rawX) {
  return map(rawX, TOUCH_MIN_X, TOUCH_MAX_X, 0, DISPLAY_WIDTH);
}

uint16_t TouchDisplay::mapTouchY(uint16_t rawY) {
  return map(rawY, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, DISPLAY_HEIGHT);
}

// UI Management functions
uint16_t TouchDisplay::addButton(uint16_t x, uint16_t y, uint16_t width,
                                 uint16_t height, const String &text,
                                 void (*callback)(uint16_t id,
                                                  TouchEvent event)) {
  if (elementCount >= maxElements)
    return 0;

  UIElement &element = uiElements[elementCount];
  element.id = elementCount + 1;
  element.type = BUTTON;
  element.x = x;
  element.y = y;
  element.width = width;
  element.height = height;
  element.text = text;
  element.color = ACCENT_COLOR;
  element.visible = true;
  element.enabled = true;
  element.callback = callback;

  drawElement(&element);
  elementCount++;

  return element.id;
}

uint16_t TouchDisplay::addLabel(uint16_t x, uint16_t y, const String &text,
                                uint16_t color) {
  if (elementCount >= maxElements)
    return 0;

  UIElement &element = uiElements[elementCount];
  element.id = elementCount + 1;
  element.type = LABEL;
  element.x = x;
  element.y = y;
  element.width = 0;
  element.height = 0;
  element.text = text;
  element.color = color;
  element.visible = true;
  element.enabled = true;
  element.callback = nullptr;

  drawElement(&element);
  elementCount++;

  return element.id;
}

uint16_t
TouchDisplay::addSlider(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                        void (*callback)(uint16_t id, TouchEvent event)) {
  if (elementCount >= maxElements)
    return 0;

  UIElement &element = uiElements[elementCount];
  element.id = elementCount + 1;
  element.type = SLIDER;
  element.x = x;
  element.y = y;
  element.width = width;
  element.height = height;
  element.text = "";
  element.color = ACCENT_COLOR;
  element.visible = true;
  element.enabled = true;
  element.callback = callback;

  drawElement(&element);
  elementCount++;

  return element.id;
}

uint16_t TouchDisplay::addToggle(uint16_t x, uint16_t y, uint16_t width,
                                 uint16_t height, const String &text,
                                 void (*callback)(uint16_t id,
                                                  TouchEvent event)) {
  if (elementCount >= maxElements)
    return 0;

  UIElement &element = uiElements[elementCount];
  element.id = elementCount + 1;
  element.type = TOGGLE;
  element.x = x;
  element.y = y;
  element.width = width;
  element.height = height;
  element.text = text;
  element.color = ACCENT_COLOR;
  element.visible = true;
  element.enabled = true;
  element.callback = callback;

  drawElement(&element);
  elementCount++;

  return element.id;
}

uint16_t TouchDisplay::addProgressBar(uint16_t x, uint16_t y, uint16_t width,
                                      uint16_t height, uint8_t progress) {
  if (elementCount >= maxElements)
    return 0;

  UIElement &element = uiElements[elementCount];
  element.id = elementCount + 1;
  element.type = PROGRESS_BAR;
  element.x = x;
  element.y = y;
  element.width = width;
  element.height = height;
  element.text = String(progress) + "%";
  element.color = ACCENT_COLOR;
  element.visible = true;
  element.enabled = true;
  element.callback = nullptr;

  drawElement(&element);
  elementCount++;

  return element.id;
}

// Element management
void TouchDisplay::removeElement(uint16_t id) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id) {
      // Shift remaining elements
      for (uint16_t j = i; j < elementCount - 1; j++) {
        uiElements[j] = uiElements[j + 1];
      }
      elementCount--;
      break;
    }
  }
}

void TouchDisplay::showElement(uint16_t id) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id) {
      uiElements[i].visible = true;
      drawElement(&uiElements[i]);
      break;
    }
  }
}

void TouchDisplay::hideElement(uint16_t id) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id) {
      uiElements[i].visible = false;
      // Redraw background to cover the element
      fillRect(uiElements[i].x, uiElements[i].y, uiElements[i].width,
               uiElements[i].height, BACKGROUND_COLOR);
      break;
    }
  }
}

void TouchDisplay::enableElement(uint16_t id) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id) {
      uiElements[i].enabled = true;
      drawElement(&uiElements[i]);
      break;
    }
  }
}

void TouchDisplay::disableElement(uint16_t id) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id) {
      uiElements[i].enabled = false;
      drawElement(&uiElements[i]);
      break;
    }
  }
}

void TouchDisplay::setElementText(uint16_t id, const String &text) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id) {
      uiElements[i].text = text;
      drawElement(&uiElements[i]);
      break;
    }
  }
}

void TouchDisplay::setElementColor(uint16_t id, uint16_t color) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id) {
      uiElements[i].color = color;
      drawElement(&uiElements[i]);
      break;
    }
  }
}

void TouchDisplay::setProgress(uint16_t id, uint8_t progress) {
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].id == id && uiElements[i].type == PROGRESS_BAR) {
      uiElements[i].text = String(progress) + "%";
      drawElement(&uiElements[i]);
      break;
    }
  }
}

// Element drawing
void TouchDisplay::drawElement(UIElement *element) {
  if (!element->visible)
    return;

  switch (element->type) {
  case BUTTON:
    // Draw button background
    fillRect(element->x, element->y, element->width, element->height,
             element->enabled ? element->color : TFT_DARKGREY);
    drawRect(element->x, element->y, element->width, element->height,
             TFT_WHITE);

    // Draw button text
    drawTextCentered(element->x + element->width / 2,
                     element->y + element->height / 2, element->text, TFT_WHITE,
                     2);
    break;

  case LABEL:
    drawText(element->x, element->y, element->text, element->color, 2);
    break;

  case SLIDER: {
    // Draw slider track
    fillRect(element->x, element->y, element->width, element->height,
             TFT_DARKGREY);
    drawRect(element->x, element->y, element->width, element->height,
             TFT_WHITE);

    // Draw slider handle
    uint16_t handleX =
        element->x + (element->width * 50) / 100; // Default to 50%
    fillCircle(handleX, element->y + element->height / 2, element->height / 2,
               element->color);
    break;
  }

  case TOGGLE: {
    // Draw toggle background
    fillRect(element->x, element->y, element->width, element->height,
             TFT_DARKGREY);
    drawRect(element->x, element->y, element->width, element->height,
             TFT_WHITE);

    // Draw toggle text
    drawText(element->x + element->width + 10,
             element->y + element->height / 2 - 8, element->text,
             element->color, 2);
    break;
  }

  case PROGRESS_BAR: {
    // Draw progress bar background
    fillRect(element->x, element->y, element->width, element->height,
             TFT_DARKGREY);
    drawRect(element->x, element->y, element->width, element->height,
             TFT_WHITE);

    // Draw progress fill
    uint8_t progress = element->text.toInt();
    uint16_t fillWidth = (element->width * progress) / 100;
    fillRect(element->x, element->y, fillWidth, element->height,
             element->color);

    // Draw progress text
    drawTextCentered(element->x + element->width / 2,
                     element->y + element->height / 2, element->text, TFT_WHITE,
                     2);
    break;
  }
  }
}

// Touch event handling
void TouchDisplay::handleTouchEvent(TouchEvent event) {
  if (!event.touched) {
    touchPressed = false;
    return;
  }

  // Check if touch is within any UI element
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].visible && uiElements[i].enabled &&
        isPointInElement(event.x, event.y, &uiElements[i])) {

      if (!touchPressed && uiElements[i].callback) {
        uiElements[i].callback(uiElements[i].id, event);
      }
      break;
    }
  }

  touchPressed = true;
  lastTouchEvent = event;
  lastTouchTime = millis();
}

bool TouchDisplay::isPointInElement(uint16_t x, uint16_t y,
                                    UIElement *element) {
  return (x >= element->x && x <= element->x + element->width &&
          y >= element->y && y <= element->y + element->height);
}

// Screen management
void TouchDisplay::drawMainScreen() {
  clear();

  // Draw title
  drawTextCentered(DISPLAY_WIDTH / 2, 20, "ESP WiFi Device", TEXT_COLOR, 3);

  // Draw status icons
  drawWiFiIcon(10, 10, true);                  // Assuming WiFi is connected
  drawBatteryIcon(DISPLAY_WIDTH - 40, 10, 75); // 75% battery
  drawSignalIcon(DISPLAY_WIDTH - 80, 10, -45); // -45 dBm signal

  // Draw main menu buttons
  addButton(50, 80, 220, 40, "Settings", nullptr);
  addButton(50, 140, 220, 40, "Status", nullptr);
  addButton(50, 200, 220, 40, "Restart", nullptr);
}

void TouchDisplay::drawSettingsScreen() {
  clear();

  drawTextCentered(DISPLAY_WIDTH / 2, 20, "Settings", TEXT_COLOR, 3);

  // Add settings controls
  addLabel(20, 60, "Brightness:", TEXT_COLOR);
  addSlider(20, 80, 200, 20, nullptr);

  addLabel(20, 120, "WiFi SSID:", TEXT_COLOR);
  addButton(20, 140, 280, 30, "Configure WiFi", nullptr);

  addButton(20, 200, 280, 30, "Back to Main", nullptr);
}

void TouchDisplay::drawStatusScreen() {
  clear();

  drawTextCentered(DISPLAY_WIDTH / 2, 20, "System Status", TEXT_COLOR, 3);

  // Add status information
  addLabel(20, 60, "WiFi: Connected", SUCCESS_COLOR);
  addLabel(20, 90, "IP: 192.168.1.100", TEXT_COLOR);
  addLabel(20, 120, "Signal: -45 dBm", TEXT_COLOR);
  addLabel(20, 150, "Uptime: 2h 15m", TEXT_COLOR);

  addProgressBar(20, 180, 280, 20, 75);
  addLabel(20, 210, "Memory Usage: 75%", TEXT_COLOR);

  addButton(20, 240, 280, 30, "Back to Main", nullptr);
}

void TouchDisplay::updateDisplay() {
  // Redraw all visible elements
  for (uint16_t i = 0; i < elementCount; i++) {
    if (uiElements[i].visible) {
      drawElement(&uiElements[i]);
    }
  }
}

// Utility drawing functions
void TouchDisplay::drawWiFiIcon(uint16_t x, uint16_t y, bool connected) {
  uint16_t color = connected ? SUCCESS_COLOR : WARNING_COLOR;

  // Draw WiFi signal bars
  for (int i = 0; i < 3; i++) {
    uint16_t barHeight = (i + 1) * 4;
    uint16_t barWidth = (i + 1) * 3;
    fillRect(x + i * 4, y + 12 - barHeight, barWidth, barHeight, color);
  }
}

void TouchDisplay::drawBatteryIcon(uint16_t x, uint16_t y, uint8_t level) {
  // Draw battery outline
  drawRect(x, y, 20, 10, TFT_WHITE);
  fillRect(x + 20, y + 3, 2, 4, TFT_WHITE);

  // Draw battery level
  uint16_t fillWidth = (18 * level) / 100;
  uint16_t color = (level > 20) ? SUCCESS_COLOR : WARNING_COLOR;
  fillRect(x + 1, y + 1, fillWidth, 8, color);
}

void TouchDisplay::drawSignalIcon(uint16_t x, uint16_t y, int8_t rssi) {
  uint16_t color;
  if (rssi > -50)
    color = SUCCESS_COLOR;
  else if (rssi > -70)
    color = TFT_YELLOW;
  else
    color = WARNING_COLOR;

  // Draw signal bars
  for (int i = 0; i < 4; i++) {
    uint16_t barHeight = (i + 1) * 3;
    fillRect(x + i * 3, y + 12 - barHeight, 2, barHeight, color);
  }
}

// Main update function
void TouchDisplay::update() {
  if (!displayInitialized)
    return;

  // Read touch events
  TouchEvent event = readTouch();
  handleTouchEvent(event);

  // Add any periodic updates here
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000) { // Update every second
    // Update status information, etc.
    lastUpdate = millis();
  }
}