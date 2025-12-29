// SDCardPins.h
//
// Board-specific microSD pin mappings (SDSPI) similar to CameraPins.h.
// Define one of the SDCARD_MODEL_* macros via build flags to pick a mapping.
//
// Notes:
// - These define *default* pins. Runtime config (config.sd.spi.*) can still
//   override them.
// - Most "smart display" ESP32 boards wire the microSD via SPI (SDSPI), not
// SDMMC.

#pragma once

// -----------------------------------------------------------------------------
// ESP32-2432S028R / "2.8 inch 240x320 smart display" microSD (SPI / VSPI)
// MISO=19, MOSI=23, SCK=18, CS=5
// -----------------------------------------------------------------------------
#if defined(SDCARD_MODEL_ESP32_2432S028R)

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
// Generic fallback (classic ESP32 VSPI defaults)
// -----------------------------------------------------------------------------
#else

#define SDCARD_SPI_MISO_GPIO_NUM 19
#define SDCARD_SPI_MOSI_GPIO_NUM 23
#define SDCARD_SPI_SCK_GPIO_NUM 18
#define SDCARD_SPI_CS_GPIO_NUM 5

#ifdef SPI3_HOST
#define SDCARD_SPI_HOST SPI3_HOST
#else
#define SDCARD_SPI_HOST SPI2_HOST
#endif

#endif
