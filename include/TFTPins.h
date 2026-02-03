// TFTPins.h
//
// Board-specific TFT + touch pin mappings.
// Define one of the ESPWiFi_TFT_MODEL_* macros via build flags to pick a
// mapping.
//
// This header mirrors the pattern used by SDCardPins.h / CameraPins.h:
// - When a model is selected: ESPWiFi_TFT_MODEL_SELECTED = 1 and GPIO macros
//   are set.
// - When no model is selected: ESPWiFi_TFT_MODEL_SELECTED = 0 and GPIO macros
//   are set to -1.
//
// Notes:
// - The ESP32-2432S028R / "Cheap Yellow Display" variants have multiple PCB
//   revisions in the wild. The mapping below is the common one where:
//   - TFT uses SPI3 (VSPI) pins (SCLK=18, MOSI=23, MISO=19) plus CS/DC/RST/BL.
//   - Touch controller (XPT2046) shares the same SPI bus pins and uses its own
//     CS + IRQ.
// - If your board revision differs, we can make these pins
// runtime-configurable,
//   but compile-time defaults are still useful for a "just works" setup.
//
#pragma once

// Pull in SPI*_HOST constants.
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) ||  \
    defined(CONFIG_IDF_TARGET_ESP32C3)
#include "driver/spi_common.h"
#endif

// -----------------------------------------------------------------------------
// ESP32-2432S028R / 2.8" 240x320 "smart display" (often ST7789 + XPT2046)
// -----------------------------------------------------------------------------
#if defined(ESPWiFi_TFT_MODEL_ESP32_2432S028R)

#define ESPWiFi_TFT_MODEL_SELECTED 1

// ESP32-2432S028R pin mapping (as provided):
// TFT_BL: 21, TFT_MISO: 12, TFT_MOSI: 13, TFT_SCLK: 14, TFT_CS: 15, TFT_DC: 2,
// TFT_RST: -1
#define TFT_SPI_HOST SPI2_HOST
#define TFT_SPI_SCK_GPIO_NUM 14
#define TFT_SPI_MOSI_GPIO_NUM 13
#define TFT_SPI_MISO_GPIO_NUM 12

// Keep bring-up simple and conservative.
#define TFT_SPI_MODE 0
#define TFT_PCLK_HZ 10000000
#define TFT_BITS_PER_PIXEL 16

#define TFT_CS_GPIO_NUM 15
#define TFT_DC_GPIO_NUM 2
// No dedicated reset GPIO on some variants; use software reset.
#define TFT_RST_GPIO_NUM -1
#define TFT_BL_GPIO_NUM 21

// Touch (XPT2046) on a separate SPI bus (matches common ESPHome configs)
#ifdef SPI3_HOST
#define TOUCH_SPI_HOST SPI3_HOST
#else
#define TOUCH_SPI_HOST SPI2_HOST
#endif
#define TOUCH_SPI_SCK_GPIO_NUM 25
#define TOUCH_SPI_MOSI_GPIO_NUM 32
#define TOUCH_SPI_MISO_GPIO_NUM 39
#define TOUCH_CS_GPIO_NUM 33
#define TOUCH_IRQ_GPIO_NUM 36

// Manual reset candidate pins for ESP32-2432S028R variants
#define TFT_RST_CANDIDATE0_GPIO_NUM -1
#define TFT_RST_CANDIDATE1_GPIO_NUM -1

// -----------------------------------------------------------------------------
// No TFT model selected
// -----------------------------------------------------------------------------
#else

#define ESPWiFi_TFT_MODEL_SELECTED 0

#ifdef SPI2_HOST
#define TFT_SPI_HOST SPI2_HOST
#else
#define TFT_SPI_HOST 0
#endif

#define TFT_SPI_SCK_GPIO_NUM -1
#define TFT_SPI_MOSI_GPIO_NUM -1
#define TFT_SPI_MISO_GPIO_NUM -1

#define TFT_SPI_MODE 0
#define TFT_PCLK_HZ 0
#define TFT_BITS_PER_PIXEL 16

#define TFT_CS_GPIO_NUM -1
#define TFT_DC_GPIO_NUM -1
#define TFT_RST_GPIO_NUM -1
#define TFT_BL_GPIO_NUM -1

#define TOUCH_SPI_HOST 0
#define TOUCH_SPI_SCK_GPIO_NUM -1
#define TOUCH_SPI_MOSI_GPIO_NUM -1
#define TOUCH_SPI_MISO_GPIO_NUM -1
#define TOUCH_CS_GPIO_NUM -1
#define TOUCH_IRQ_GPIO_NUM -1

#define TFT_RST_CANDIDATE0_GPIO_NUM -1
#define TFT_RST_CANDIDATE1_GPIO_NUM -1

#endif

// Single helper macro for "TFT feature compiled in?"
#if ESPWiFi_TFT_MODEL_SELECTED
#define ESPWiFi_HAS_TFT 1
#else
#define ESPWiFi_HAS_TFT 0
#endif
