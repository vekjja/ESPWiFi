// SDCardPins.h
//
// Board-specific microSD pin mappings (SDSPI) similar to CameraPins.h.
// Define one of the ESPWiFi_SDCARD_MODEL_* macros via build flags to pick a
// mapping.
//
// Notes:
// - These define *default* pins. Runtime config (config.sd.spi.*) can still
//   override them.
// - Most "smart display" ESP32 boards wire the microSD via SPI (SDSPI), not
//   SDMMC.

#pragma once

// Pull in SPI*_HOST constants (SPI2_HOST, SPI3_HOST).
// (CameraPins.h doesn't need this; SD does because we expose a default SPI
// host.)
#include "sdkconfig.h"
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) ||  \
    defined(CONFIG_IDF_TARGET_ESP32C3)
#include "driver/spi_common.h"
#endif

// -----------------------------------------------------------------------------
// Seeed XIAO ESP32-S3 Sense microSD card slot (SPI)
// CS=21, SCK=7 (D8), MISO=8 (D9), MOSI=9 (D10)
// -----------------------------------------------------------------------------
#if defined(ESPWiFi_SDCARD_MODEL_SEEED_XIAO_ESP32S3)

#define ESPWiFi_SDCARD_MODEL_SELECTED 1

#define SDCARD_SPI_MISO_GPIO_NUM 8
#define SDCARD_SPI_MOSI_GPIO_NUM 9
#define SDCARD_SPI_SCK_GPIO_NUM 7
#define SDCARD_SPI_CS_GPIO_NUM 21

// ESP32-S3 uses SPI2_HOST
#define SDCARD_SPI_HOST SPI2_HOST

// -----------------------------------------------------------------------------
// ESP32-2432S028R / "2.8 inch 240x320 smart display" microSD (SPI / VSPI)
// MISO=19, MOSI=23, SCK=18, CS=5
// -----------------------------------------------------------------------------
#elif defined(ESPWiFi_SDCARD_MODEL_ESP32_2432S028R)

#define ESPWiFi_SDCARD_MODEL_SELECTED 1

#define SDCARD_SPI_MISO_GPIO_NUM 19
#define SDCARD_SPI_MOSI_GPIO_NUM 23
#define SDCARD_SPI_SCK_GPIO_NUM 18
#define SDCARD_SPI_CS_GPIO_NUM 5

// Prefer VSPI (SPI3_HOST) on classic ESP32 when available
#ifdef SPI3_HOST
#define SDCARD_SPI_HOST SPI3_HOST
#else
#define SDCARD_SPI_HOST SPI2_HOST
#endif

// -----------------------------------------------------------------------------
// No SD model selected: mark pins invalid so SD stays disabled unless enabled
// by a board profile.
// -----------------------------------------------------------------------------
#else

#define ESPWiFi_SDCARD_MODEL_SELECTED 0

#define SDCARD_SPI_MISO_GPIO_NUM -1
#define SDCARD_SPI_MOSI_GPIO_NUM -1
#define SDCARD_SPI_SCK_GPIO_NUM -1
#define SDCARD_SPI_CS_GPIO_NUM -1

#ifdef SPI3_HOST
#define SDCARD_SPI_HOST SPI3_HOST
#else
#define SDCARD_SPI_HOST SPI2_HOST
#endif

#endif

// Single helper macro for "SD feature compiled in?"
// Mirrors CameraPins.h: feature is "present" only when a model is selected.
#if ESPWiFi_SDCARD_MODEL_SELECTED
#define ESPWiFi_HAS_SDCARD 1
#else
#define ESPWiFi_HAS_SDCARD 0
#endif
