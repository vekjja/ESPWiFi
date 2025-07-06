# Touch Display Integration for ESPWiFi

This document explains how to use the touch display functionality with your ESP32 touch display device.

## Hardware Requirements

- ESP32 development board with touch display (like the one from [Amazon](https://www.amazon.com/dp/B0F242GFHK))
- TFT display (typically ILI9341 or similar)
- XPT2046 touch controller
- Appropriate wiring connections

## Pin Configuration

The touch display uses the following pins:

| Function | Pin | Description |
|----------|-----|-------------|
| TFT_CS   | 15  | TFT Chip Select |
| TFT_DC   | 2   | TFT Data/Command |
| TFT_RST  | 4   | TFT Reset |
| TFT_BL   | 21  | TFT Backlight |
| TOUCH_CS | 5   | Touch Chip Select |
| TOUCH_IRQ| 2   | Touch Interrupt |

## Installation

1. **Install Required Libraries**
   The following libraries are automatically installed via PlatformIO:
   - `TFT_eSPI` - TFT display driver
   - `XPT2046_Touchscreen` - Touch controller driver

2. **Configuration Files**
   - `User_Setup.h` - TFT_eSPI configuration
   - `include/TouchDisplay.h` - Touch display header
   - `src/TouchDisplay.cpp` - Touch display implementation

## Usage

### Basic Setup

```cpp
#include <TouchDisplay.h>

// Global instance
TouchDisplay display;

void setup() {
  // Initialize touch display
  if (display.begin()) {
    Serial.println("Touch display initialized successfully");
  }
  
  // Draw main screen
  display.drawMainScreen();
}

void loop() {
  // Update touch display (call this regularly)
  display.update();
}
```

### Creating UI Elements

#### Buttons
```cpp
// Add a button with callback
uint16_t buttonId = display.addButton(50, 80, 220, 40, "Settings", onSettingsButton);

// Callback function
void onSettingsButton(uint16_t id, TouchEvent event) {
  display.drawSettingsScreen();
}
```

#### Labels
```cpp
// Add a text label
uint16_t labelId = display.addLabel(20, 60, "WiFi Status:", TEXT_COLOR);
```

#### Sliders
```cpp
// Add a slider with callback
uint16_t sliderId = display.addSlider(20, 80, 200, 20, onBrightnessSlider);

// Callback function
void onBrightnessSlider(uint16_t id, TouchEvent event) {
  uint8_t brightness = map(event.x, 20, 220, 0, 255);
  display.setBrightness(brightness);
}
```

#### Progress Bars
```cpp
// Add a progress bar
uint16_t progressId = display.addProgressBar(20, 180, 280, 20, 75);

// Update progress
display.setProgress(progressId, 85);
```

### Screen Management

The touch display comes with pre-built screens:

#### Main Screen
```cpp
display.drawMainScreen();
```
Shows:
- Device title
- Status icons (WiFi, battery, signal)
- Main menu buttons (Settings, Status, Restart)

#### Settings Screen
```cpp
display.drawSettingsScreen();
```
Shows:
- Brightness slider
- WiFi configuration button
- Back button

#### Status Screen
```cpp
display.drawStatusScreen();
```
Shows:
- WiFi connection status
- IP address
- Signal strength
- Uptime
- Memory usage
- Back button

### Custom Drawing

#### Text Drawing
```cpp
// Draw text at specific position
display.drawText(20, 60, "Hello World", TEXT_COLOR, 2);

// Draw centered text
display.drawTextCentered(160, 120, "Centered Text", TEXT_COLOR, 3);
```

#### Shapes
```cpp
// Draw rectangles
display.drawRect(10, 10, 100, 50, TFT_RED);
display.fillRect(120, 10, 100, 50, TFT_BLUE);

// Draw circles
display.drawCircle(50, 100, 25, TFT_GREEN);
display.fillCircle(150, 100, 25, TFT_YELLOW);

// Draw lines
display.drawLine(10, 150, 310, 150, TFT_WHITE);
```

#### Status Icons
```cpp
// Draw WiFi icon
display.drawWiFiIcon(10, 10, true);  // true = connected

// Draw battery icon
display.drawBatteryIcon(50, 10, 75); // 75% battery

// Draw signal icon
display.drawSignalIcon(90, 10, -45); // -45 dBm signal
```

### Element Management

#### Show/Hide Elements
```cpp
display.showElement(elementId);
display.hideElement(elementId);
```

#### Enable/Disable Elements
```cpp
display.enableElement(elementId);
display.disableElement(elementId);
```

#### Update Element Properties
```cpp
display.setElementText(elementId, "New Text");
display.setElementColor(elementId, TFT_RED);
```

### Touch Event Handling

Touch events are automatically handled by the display system. When a touch is detected on a UI element, the associated callback function is called:

```cpp
void onTouchCallback(uint16_t id, TouchEvent event) {
  // event.touched - true if screen is being touched
  // event.x, event.y - touch coordinates
  // event.pressure - touch pressure (if supported)
  
  if (event.touched) {
    Serial.printf("Touch at (%d, %d)\n", event.x, event.y);
  }
}
```

## Configuration

### Touch Calibration

If touch coordinates are not accurate, you can calibrate the touch screen:

```cpp
display.calibrateTouch();
```

You may need to adjust the calibration values in `TouchDisplay.h`:

```cpp
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3800
```

### Display Brightness

```cpp
// Set brightness (0-255)
display.setBrightness(128);

// Turn backlight on/off
display.setBacklight(true);
display.setBacklight(false);
```

### Display Rotation

The display is configured for landscape orientation. To change this, modify the `begin()` function in `TouchDisplay.cpp`:

```cpp
tft.setRotation(1); // 0=Portrait, 1=Landscape, 2=Portrait flipped, 3=Landscape flipped
```

## Integration with ESPWiFi

The touch display is fully integrated with the ESPWiFi framework:

1. **Automatic Initialization**: The display is initialized in `setup()`
2. **Regular Updates**: The display is updated in the main `loop()`
3. **Logging**: Display status is logged through the ESPWiFi logging system
4. **Configuration**: Display settings can be stored in the ESPWiFi configuration

### Example Integration

```cpp
#include <ESPWiFi.h>
#include <TouchDisplay.h>

ESPWiFi device;

void setup() {
  device.startLog();
  
  // Initialize touch display
  if (display.begin()) {
    device.log("Touch display initialized successfully");
  } else {
    device.log("Failed to initialize touch display");
  }
  
  device.startWiFi();
  device.startMDNS();
  device.startWebServer();
  
  // Draw main screen
  display.drawMainScreen();
}

void loop() {
  yield();
  
  // Update touch display
  display.update();
  
  // ESPWiFi functions
  device.streamRSSI();
}
```

## Troubleshooting

### Display Not Working
1. Check pin connections
2. Verify `User_Setup.h` configuration
3. Ensure libraries are installed
4. Check serial output for error messages

### Touch Not Working
1. Verify touch pin connections
2. Run touch calibration
3. Adjust calibration values
4. Check for pin conflicts

### Performance Issues
1. Reduce update frequency
2. Disable unused features
3. Optimize drawing operations
4. Use hardware SPI

### Memory Issues
1. Reduce number of UI elements
2. Use smaller fonts
3. Optimize image sizes
4. Enable PSRAM if available

## Advanced Features

### Custom Screens
Create custom screens by combining drawing functions and UI elements:

```cpp
void drawCustomScreen() {
  display.clear();
  
  // Draw background
  display.fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, TFT_NAVY);
  
  // Draw title
  display.drawTextCentered(DISPLAY_WIDTH / 2, 20, "Custom Screen", TFT_WHITE, 3);
  
  // Add custom elements
  display.addButton(50, 80, 220, 40, "Custom Button", onCustomButton);
  display.addLabel(20, 140, "Custom Label", TFT_YELLOW);
}
```

### Animation
Create simple animations by updating elements in the loop:

```cpp
unsigned long lastAnimation = 0;
uint8_t animationFrame = 0;

void loop() {
  display.update();
  
  // Animate every 100ms
  if (millis() - lastAnimation > 100) {
    animationFrame = (animationFrame + 1) % 4;
    
    // Update animation
    display.setElementText(animationLabelId, "Frame " + String(animationFrame));
    
    lastAnimation = millis();
  }
}
```

## Support

For issues and questions:
1. Check the serial output for error messages
2. Verify hardware connections
3. Test with minimal code first
4. Consult the TFT_eSPI and XPT2046_Touchscreen library documentation

## License

This touch display implementation is part of the ESPWiFi project and follows the same license terms. 