#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem

#include "ESPWiFi.h"
#include "SDCardPins.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"
#if defined(CONFIG_IDF_TARGET_ESP32)
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#endif
#include <ArduinoJson.h>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

void ESPWiFi::initLittleFS() {
  if (lfs != nullptr) {
    return;
  }

  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/lfs",
      .partition_label = "littlefsp",
      .partition = nullptr,
      .format_if_mount_failed = true,
      .read_only = false,
      .dont_mount = false,
      .grow_on_mount = false,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      printf("Failed to mount or format filesystem\n");
    } else if (ret == ESP_ERR_NOT_FOUND) {
      printf("Failed to find LittleFS partition\n");
    } else {
      printf("Failed to initialize LittleFS (%s)\n", esp_err_to_name(ret));
    }
    lfs = nullptr;
    return;
  }

  // Set lfs to non-null to indicate LittleFS is initialized
  // esp_vfs_littlefs_register doesn't return a handle, so we use a sentinel
  // value
  lfs = (void *)0x1;
  log(INFO, "💾 LittleFS Initialized");
}

#if defined(CONFIG_IDF_TARGET_ESP32)
// Helper: Get default SPI pin configuration from SDCardPins.h
static void getSpiPinConfig(int &mosi, int &miso, int &sclk, int &cs,
                            int &hostId) {
  // Use hardware-defined default pins from SDCardPins.h
  mosi = SDCARD_SPI_MOSI_GPIO_NUM;
  miso = SDCARD_SPI_MISO_GPIO_NUM;
  sclk = SDCARD_SPI_SCK_GPIO_NUM;
  cs = SDCARD_SPI_CS_GPIO_NUM;
  hostId = SDCARD_SPI_HOST; // Use default SPI host from SDCardPins.h
}

// Helper: Initialize SPI bus for SD card
static esp_err_t initSpiBus(spi_host_device_t hostId, int mosi, int miso,
                            int sclk, bool &busOwned) {
  busOwned = false;

  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = mosi;
  bus_cfg.miso_io_num = miso;
  bus_cfg.sclk_io_num = sclk;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;

  esp_err_t ret = spi_bus_initialize(hostId, &bus_cfg, SPI_DMA_CH_AUTO);
  if (ret == ESP_OK) {
    busOwned = true;
    return ESP_OK;
  } else if (ret == ESP_ERR_INVALID_STATE) {
    // Bus already initialized elsewhere (e.g., by LCD driver)
    busOwned = false;
    return ESP_OK;
  }
  return ret;
}

// Helper: Cleanup SPI bus if we own it
static void cleanupSpiBus(spi_host_device_t hostId, bool busOwned) {
  if (busOwned && hostId >= 0) {
    (void)spi_bus_free(hostId);
  }
}
#endif

void ESPWiFi::initSDCard() {
  // Early return if already initialized
  if (sdCard != nullptr) {
    return;
  }

  // Initialize SPI state (needed for cleanup)
  sdSpiBusOwned = false;
  sdSpiHost = -1;

  // Initialize logging state
  sdInitAttempted = true;
  sdInitLastErr = ESP_OK;
  sdNotSupported = false;

  log(INFO, "💾 SD Card Initializing, Mount Point: %s", sdMountPoint.c_str());
  feedWatchDog(); // Yield before heavy operations to reduce stack pressure

#if defined(CONFIG_IDF_TARGET_ESP32)
  // Auto-detect: try SPI first, then SDMMC
  // Configure mount parameters
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 16 * 1024;

  esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
  sdmmc_card_t *card = nullptr;

  // Try SPI interface first
  {
    // Get default pin configuration
    int mosi, miso, sclk, cs, hostId;
    getSpiPinConfig(mosi, miso, sclk, cs, hostId);

    // Validate pins
    if (cs < 0 || sclk < 0 || mosi < 0 || miso < 0) {
      sdNotSupported = true;
      sdInitLastErr = ESP_ERR_INVALID_ARG;
      log(WARNING, "💾 SD(SPI) invalid pin configuration");
      return;
    }

    // Use SPI host from SDCardPins.h
    spi_host_device_t spiHost = (spi_host_device_t)hostId;
    sdSpiHost = spiHost;

    log(DEBUG, "💾 SD(SPI) config: host=%d, mosi=%d, miso=%d, sclk=%d, cs=%d",
        spiHost, mosi, miso, sclk, cs);
    feedWatchDog(); // Yield before SPI bus init

    // Initialize SPI bus
    ret = initSpiBus(spiHost, mosi, miso, sclk, sdSpiBusOwned);
    if (ret != ESP_OK) {
      sdSpiHost = -1;
      sdInitLastErr = ret;
      log(WARNING, "💾 SD(SPI) bus init failed: %s", esp_err_to_name(ret));
      return;
    }
    feedWatchDog(); // Yield after SPI bus init, before mount

    // Configure SD card device
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = spiHost;
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)cs;
    slot_config.host_id = spiHost;

    // Mount SD card (this can use significant stack)
    ret = esp_vfs_fat_sdspi_mount(sdMountPoint.c_str(), &host, &slot_config,
                                  &mount_config, &card);
    if (ret != ESP_OK) {
      // Cleanup SPI bus on mount failure
      cleanupSpiBus(spiHost, sdSpiBusOwned);
      sdSpiBusOwned = false;
      sdSpiHost = -1;
      sdInitLastErr = ret;
      // SPI failed, will try SDMMC below
      log(WARNING, "💾 SD(SPI) Mount Failed: %s", esp_err_to_name(ret));
    } else {
      // Success
      sdCard = (void *)card;
      log(INFO, "💾 SD(SPI) Mounted: %s", sdMountPoint.c_str());
      config["sd"]["initialized"] = true;
    }
    feedWatchDog(); // Yield after SPI mount attempt
  }

  // Try SDMMC (native interface) if SPI failed
  if (ret != ESP_OK) {
    feedWatchDog(); // Yield before SDMMC attempt
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;

    ret = esp_vfs_fat_sdmmc_mount(sdMountPoint.c_str(), &host, &slot_config,
                                  &mount_config, &card);
    if (ret == ESP_OK) {
      sdCard = (void *)card;
      log(INFO, "💾 SD(SDMMC) Mounted: %s", sdMountPoint.c_str());
      config["sd"]["initialized"] = true;
    } else {
      // Both SPI and SDMMC failed
      log(WARNING, "💾 SD(SDMMC) Mount Failed: %s", esp_err_to_name(ret));
    }
  }

  // Handle final error state
  if (ret != ESP_OK) {
    sdCard = nullptr;
    sdInitLastErr = ret;
    sdNotSupported = false;
    // Final cleanup of any remaining SPI state
    if (sdSpiHost >= 0 && sdSpiBusOwned) {
      cleanupSpiBus((spi_host_device_t)sdSpiHost, sdSpiBusOwned);
    }
    sdSpiBusOwned = false;
    sdSpiHost = -1;
    log(ERROR, "💾 SD Card Mount Failed: %s", esp_err_to_name(ret));
    config["sd"]["initialized"] = false;
  }
#else
  // For non-ESP32 targets, SD wiring varies widely
  sdCard = nullptr;
  sdNotSupported = true;
  config["sd"]["initialized"] = false;
  sdInitLastErr = ESP_ERR_NOT_SUPPORTED;
  log(ERROR, "💾 SD Not Supported for this Device: %s", esp_err_to_name(ret));
#endif
}

void ESPWiFi::deinitSDCard() {
  if (sdCard == nullptr) {
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32)
  // Best-effort unmount. This can fail if card was never mounted correctly.
  // Clear sdCard pointer first to prevent other code from trying to use it
  sdmmc_card_t *card = (sdmmc_card_t *)sdCard;
  sdCard = nullptr; // Clear before unmount to prevent use-after-free

  // Unmount may fail if card is already gone - ignore errors
  (void)esp_vfs_fat_sdcard_unmount(sdMountPoint.c_str(), card);

  if (sdSpiBusOwned && sdSpiHost >= 0) {
    (void)spi_bus_free((spi_host_device_t)sdSpiHost);
  }
#endif

  config["sd"]["initialized"] = false;
  sdSpiBusOwned = false;
  sdCard = nullptr;
  sdSpiHost = -1;
}

bool ESPWiFi::checkSDCard() {
  if (sdCardCheck.shouldRun()) {
    if (sdCard != nullptr) {
      // Card is mounted - verify it's still present
      // Use lightweight filesystem check - stat() will fail fast if card is
      // removed
      errno = 0;
      struct stat st;
      if (stat(sdMountPoint.c_str(), &st) != 0) {
        // Check if it's an I/O error (card removed) vs other error
        if (errno == 5) { // EIO - Input/output error
          deinitSDCard();
          return false;
        }
        // Other errors (ENOENT, etc.) might be transient - assume present
        // But if errno is still 0, something weird happened - assume not
        // present
        if (errno == 0) {
          deinitSDCard();
          return false;
        }
        return true;
      }

      // Mount point accessible - verify it's actually a directory
      if (!S_ISDIR(st.st_mode)) {
        deinitSDCard();
        return false;
      }
      return true;
    } else if (!sdNotSupported) {
      // Card was removed - try to reinitialize if it's been reinserted
      initSDCard();
      if (sdCard != nullptr) {
        log(INFO, "🔄 💾 SD Card Remounted: %s", sdMountPoint.c_str());
      }
    }
  }
  return true;
}

void ESPWiFi::handleSDCardError() {
  // Called when SD card operations fail - mark as unavailable
  // The card will be automatically re-detected in runSystem() if reinserted
  if (sdCard != nullptr) {
    log(WARNING, "💾 SD Card Error Detected, Unmounting");
    deinitSDCard();
  }
}

std::string ESPWiFi::sanitizeFilename(const std::string &filename) {
  std::string sanitized = filename;

  // Replace spaces with underscores
  for (char &c : sanitized) {
    if (c == ' ')
      c = '_';
    else if (!isalnum(c) && c != '.' && c != '-' && c != '_' && c != '/') {
      c = '_';
    }
  }

  // Remove duplicate underscores
  size_t pos;
  while ((pos = sanitized.find("__")) != std::string::npos) {
    sanitized.replace(pos, 2, "_");
  }

  // Trim leading/trailing underscores
  if (!sanitized.empty() && sanitized[0] == '_') {
    sanitized = sanitized.substr(1);
  }
  if (!sanitized.empty() && sanitized[sanitized.length() - 1] == '_') {
    sanitized = sanitized.substr(0, sanitized.length() - 1);
  }

  return sanitized;
}

void ESPWiFi::getStorageInfo(const std::string &fsParam, size_t &totalBytes,
                             size_t &usedBytes, size_t &freeBytes) {
  totalBytes = 0;
  usedBytes = 0;
  freeBytes = 0;

  if (fsParam == "lfs" && lfs != nullptr) {
    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info("littlefsp", &total, &used);
    if (ret == ESP_OK) {
      totalBytes = total;
      usedBytes = used;
      freeBytes = total - used;
    }
    return;
  }

  if (fsParam == "sd" && sdCard != nullptr) {
    // FATFS free/total calculation (best-effort).
    FATFS *fs = nullptr;
    DWORD fre_clust = 0;
    FRESULT fr = f_getfree("0:", &fre_clust, &fs);
    if (fr == FR_OK && fs != nullptr) {
      const uint64_t bytesPerClust = (uint64_t)fs->csize * 512ULL;
      const uint64_t totalClust = (uint64_t)(fs->n_fatent - 2);
      const uint64_t total = totalClust * bytesPerClust;
      const uint64_t freeb = (uint64_t)fre_clust * bytesPerClust;
      totalBytes = (size_t)total;
      freeBytes = (size_t)freeb;
      usedBytes = (totalBytes >= freeBytes) ? (totalBytes - freeBytes) : 0;
    }
  }
}

void ESPWiFi::logFilesystemInfo(const std::string &fsName, size_t totalBytes,
                                size_t usedBytes) {
  log(INFO, "💾 %s Filesystem", fsName.c_str());
  log(DEBUG, "💾\tTotal: %s", bytesToHumanReadable(totalBytes).c_str());
  log(DEBUG, "💾\tUsed: %s", bytesToHumanReadable(usedBytes).c_str());
  log(DEBUG, "💾\tFree: %s",
      bytesToHumanReadable(totalBytes - usedBytes).c_str());
}

void ESPWiFi::printFilesystemInfo() {

  if (lfs != nullptr) {
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("LittleFS", totalBytes, usedBytes);
  }

  if (sdCard != nullptr) {
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("sd", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("SD", totalBytes, usedBytes);
    return;
  }

  // SD card not available - log status if we attempted detection
  if (sdInitAttempted) {
    if (sdNotSupported) {
      log(DEBUG, "💾 SD card not available: not configured for this target\n"
                 "Configure SPI pins in config (SDCardPins.h) to enable SD "
                 "card support");
    } else if (sdInitLastErr != ESP_OK) {
      log(DEBUG, "💾 SD card not detected: %s", esp_err_to_name(sdInitLastErr));
    } else {
      log(DEBUG, "💾 SD card not detected");
    }
  }
}

bool ESPWiFi::deleteDirectoryRecursive(const std::string &dirPath) {
  if (lfs == nullptr)
    return false;

  std::string full_path = lfsMountPoint + dirPath;
  DIR *dir = opendir(full_path.c_str());
  if (!dir)
    return false;

  struct dirent *entry;
  int entryCount = 0;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string entry_path = dirPath + "/" + entry->d_name;
    std::string full_entry_path = lfsMountPoint + entry_path;

    struct stat st;
    if (stat(full_entry_path.c_str(), &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        deleteDirectoryRecursive(entry_path);
      } else {
        ::remove(full_entry_path.c_str());
      }
    }

    // Yield periodically to prevent watchdog timeout
    if (++entryCount % 10 == 0) {
      feedWatchDog();
    }
  }
  closedir(dir);

  std::string full_dir_path = lfsMountPoint + dirPath;
  return ::rmdir(full_dir_path.c_str()) == 0;
}

bool ESPWiFi::writeFile(const std::string &filePath, const uint8_t *data,
                        size_t len) {
  if (lfs == nullptr)
    return false;

  // Use atomic write: write to temp file first, then rename
  // This prevents corruption if write fails partway through
  std::string full_path = lfsMountPoint + filePath;
  std::string temp_path = full_path + ".tmp";

  // Remove temp file if it exists (leftover from previous failed write)
  ::remove(temp_path.c_str());

  FILE *f = fopen(temp_path.c_str(), "wb");
  if (!f) {
    return false;
  }

  size_t bytesWritten = fwrite(data, 1, len, f);
  feedWatchDog(); // Yield after file write

  // Flush the file to ensure data is written to filesystem
  if (fflush(f) != 0) {
    fclose(f);
    ::remove(temp_path.c_str()); // Clean up temp file
    return false;
  }

  fclose(f);

  if (bytesWritten != len) {
    ::remove(temp_path.c_str()); // Clean up temp file
    return false;
  }

  // Atomically replace the file with the temp file
  if (rename(temp_path.c_str(), full_path.c_str()) != 0) {
    ::remove(temp_path.c_str()); // Clean up temp file
    return false;
  }

  return true;
}

char *ESPWiFi::readFile(const std::string &filePath, size_t *outSize) {
  if (lfs == nullptr) {
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  std::string full_path = lfsMountPoint + filePath;
  FILE *f = fopen(full_path.c_str(), "rb");
  if (!f) {
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  // Get file size
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  long fileSize = ftell(f);
  if (fileSize < 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  if (fileSize == 0) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  // Allocate buffer (caller must free)
  char *buffer = (char *)malloc(fileSize + 1);
  if (!buffer) {
    fclose(f);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  size_t bytesRead = fread(buffer, 1, fileSize, f);
  fclose(f);

  if (bytesRead != (size_t)fileSize) {
    free(buffer);
    if (outSize)
      *outSize = 0;
    return nullptr;
  }

  buffer[bytesRead] = '\0'; // Null terminate for string operations

  if (outSize)
    *outSize = bytesRead;

  return buffer;
}

bool ESPWiFi::isProtectedFile(const std::string &fsParam,
                              const std::string &filePath) {
  (void)fsParam; // Protection is config-driven and applies to all filesystems.

  // Hard-coded protection: the active config file must never be modifiable via
  // HTTP file APIs, even if not listed in auth.protectFiles.
  std::string normalizedPath = filePath;
  if (!normalizedPath.empty() && normalizedPath.front() != '/') {
    normalizedPath.insert(normalizedPath.begin(), '/');
  }
  if (!configFile.empty() && normalizedPath == configFile) {
    return true;
  }

  // First: config-driven protection (applies to BOTH LFS and SD if configured).
  // These paths are "protected": no filesystem operation should be allowed via
  // the HTTP API, even if the request is authenticated.
  JsonVariant protectedFiles = config["auth"]["protectFiles"];
  if (protectedFiles.is<JsonArray>()) {
    // filePath is expected to be normalized (leading '/', no trailing '/'
    // unless it's root). Be defensive anyway: normalize minimally so config
    // patterns like "config.json" match even if callers pass "config.json".
    std::string_view path(normalizedPath);
    for (JsonVariant v : protectedFiles.as<JsonArray>()) {
      const char *pat = v.as<const char *>();
      if (pat == nullptr || pat[0] == '\0') {
        continue;
      }

      std::string patternStr(pat);
      // Allow config entries like "index.html" or "static/*" (no leading '/').
      if (!patternStr.empty() && patternStr.front() != '/') {
        patternStr.insert(patternStr.begin(), '/');
      }

      std::string_view pattern(patternStr);

      // Special-case "/" so it matches ONLY the root path.
      if (pattern == "/") {
        if (path == "/") {
          return true;
        }
        continue;
      }

      if (matchPattern(path, pattern)) {
        return true;
      }
    }
  }

  return false;
}

std::string ESPWiFi::getFileExtension(const std::string &filename) {
  size_t pos = filename.find_last_of('.');
  if (pos != std::string::npos) {
    // Return extension after the dot
    return filename.substr(pos + 1);
  }
  // If no dot found, return the filename itself
  return filename;
}

// File upload handler - used for chunked uploads if needed
// Currently uploads are handled directly in the /api/files/upload endpoint
void ESPWiFi::handleFileUpload(void *req, const std::string &filename,
                               size_t index, uint8_t *data, size_t len,
                               bool final) {
  // This function signature matches the original AsyncWebServer pattern
  // For ESP-IDF, we handle uploads directly in the endpoint handler
  // This stub is kept for API compatibility
  (void)req;
  (void)filename;
  (void)index;
  (void)data;
  (void)len;
  (void) final;
}

#endif // ESPWiFi_FileSystem
