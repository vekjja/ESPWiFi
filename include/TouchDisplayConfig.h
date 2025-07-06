// TouchDisplayConfig.h - Configuration file for TFT_eSPI library
// Configured for ESP32 touch display with ILI9341 TFT and XPT2046 touch
// controller

#define USER_SETUP_ID 1

// ##################################################################################
//
// Section 1. Define the pins that are used to interface with the display here
//
// ##################################################################################

// ESP32 pins
#define TFT_CS 15 // Chip select control pin
#define TFT_DC 2  // Data Command control pin
#define TFT_RST 4 // Reset pin
#define TFT_BL 21 // LED back-light

// Touch pins
#define TOUCH_CS 5  // Touch chip select pin
#define TOUCH_IRQ 2 // Touch interrupt pin

// ##################################################################################
//
// Section 2. Define the display driver type here
//
// ##################################################################################

#define ILI9341_DRIVER

// ##################################################################################
//
// Section 3. Define the display resolution here
//
// ##################################################################################

#define TFT_WIDTH 320
#define TFT_HEIGHT 240

// ##################################################################################
//
// Section 4. Other options
//
// ##################################################################################

// Color depth
#define LOAD_GLCD // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in
                  // FLASH
#define LOAD_FONT2 // Font 2. Small 16 pixel font, needs ~3534 bytes in FLASH,
                   // 96 characters
#define LOAD_FONT4 // Font 4. Medium 26 pixel font, needs ~5848 bytes in FLASH,
                   // 96 characters
#define LOAD_FONT6 // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH,
                   // only characters 1234567890:-.apm
#define LOAD_FONT7 // Font 7. 7 segment 48 pixel high font, needs ~2438 bytes in
                   // FLASH, only characters 1234567890:.
#define LOAD_FONT8 // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH,
                   // only characters 1234567890:-.
#define LOAD_GFXFF // FreeFonts. Include access to the 48 Adafruit_GFX free
                   // fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT

// SPI frequency
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000

// Optional reduced SPI pins for ESP32
#define TFT_SDA_READ // This option allows the library to read SPI data from the
                     // TFT

// For ESP32 Dev board (only tested with ILI9341 display)
// The hardware SPI can be mapped to any pins
#define VSPI_HOST SPI2_HOST
#define HSPI_HOST SPI3_HOST

// ##################################################################################
//
// Section 5. Touch configuration
//
// ##################################################################################

// Touch calibration values (adjust these based on your specific display)
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3800
#define TOUCH_MIN_Y 200
#define TOUCH_MAX_Y 3800

// ##################################################################################
//
// Section 6. Other configurations
//
// ##################################################################################

// Enable DMA (if supported)
#define DMA_ENABLE

// Enable smooth fonts
#define SMOOTH_FONT

// Enable anti-aliased fonts
#define SMOOTH_FONT

// Enable JPEG decoder
#define JPEG_ENABLE

// Enable PNG decoder
#define PNG_ENABLE

// Enable BMP decoder
#define BMP_ENABLE

// Enable GIF decoder
#define GIF_ENABLE

// Enable WebP decoder
#define WEBP_ENABLE

// Enable SPIFFS support
#define SPIFFS_ENABLE

// Enable LittleFS support
#define LITTLEFS_ENABLE

// ##################################################################################
//
// Section 7. Debug options
//
// ##################################################################################

// Uncomment to enable debug output
// #define DEBUG_TFT_eSPI

// ##################################################################################
//
// Section 8. Performance options
//
// ##################################################################################

// Enable frame buffer (uses more RAM but faster rendering)
// #define TFT_FRAMEBUFFER

// Enable double buffering
// #define TFT_DOUBLEBUFFER

// ##################################################################################
//
// Section 9. Color definitions
//
// ##################################################################################

// Define colors if not already defined
#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif

#ifndef TFT_NAVY
#define TFT_NAVY 0x000F
#endif

#ifndef TFT_DARKGREEN
#define TFT_DARKGREEN 0x03E0
#endif

#ifndef TFT_DARKCYAN
#define TFT_DARKCYAN 0x03EF
#endif

#ifndef TFT_MAROON
#define TFT_MAROON 0x7800
#endif

#ifndef TFT_PURPLE
#define TFT_PURPLE 0x780F
#endif

#ifndef TFT_OLIVE
#define TFT_OLIVE 0x7BE0
#endif

#ifndef TFT_LIGHTGREY
#define TFT_LIGHTGREY 0xC618
#endif

#ifndef TFT_DARKGREY
#define TFT_DARKGREY 0x7BEF
#endif

#ifndef TFT_BLUE
#define TFT_BLUE 0x001F
#endif

#ifndef TFT_GREEN
#define TFT_GREEN 0x07E0
#endif

#ifndef TFT_CYAN
#define TFT_CYAN 0x07FF
#endif

#ifndef TFT_RED
#define TFT_RED 0xF800
#endif

#ifndef TFT_MAGENTA
#define TFT_MAGENTA 0xF81F
#endif

#ifndef TFT_YELLOW
#define TFT_YELLOW 0xFFE0
#endif

#ifndef TFT_WHITE
#define TFT_WHITE 0xFFFF
#endif

#ifndef TFT_ORANGE
#define TFT_ORANGE 0xFDA0
#endif

#ifndef TFT_GREENYELLOW
#define TFT_GREENYELLOW 0xB7E0
#endif

#ifndef TFT_PINK
#define TFT_PINK 0xFC9F
#endif