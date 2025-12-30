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
#include <sys/stat.h>
#include <time.h>

void ESPWiFi::initLittleFS() {
  if (littleFsInitialized) {
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
    return;
  }

  littleFsInitialized = true;
  log(INFO, "💾 LittleFS Initialized");
}

void ESPWiFi::initSDCard() {
  if (sdCardInitialized) {
    // Keep config in sync (in case config was loaded without this field).
    config["sd"]["initialized"] = true;
    return;
  }

  sdInitAttempted = true;
  sdInitLastErr = ESP_OK;
  sdInitNotConfiguredForTarget = false;
  sdSpiBusOwned = false;
  sdSpiHost = -1;

  // Config-gated: keep boot fast / deterministic unless explicitly enabled.
  bool enabled = false;
  std::string type = "auto";
  if (config["sd"].is<JsonObject>()) {
    enabled = config["sd"]["enabled"] | false;
    if (!config["sd"]["type"].isNull()) {
      type = config["sd"]["type"].as<std::string>();
    } else if (!config["sd"]["mode"].isNull()) {
      type = config["sd"]["mode"].as<std::string>();
    }
  }
  if (!enabled) {
    sdCardInitialized = false;
    config["sd"]["initialized"] = false;
    // Not an error; SD just disabled.
    sdInitAttempted = false;
    return;
  }

  log(INFO, "💾 SD enabled: attempting mount at %s", sdMountPoint.c_str());

  // Mount FATFS on SD. This is target/board specific; we implement a safe
  // default for SDMMC-capable targets. If this fails, APIs will report SD as
  // unavailable.
#if defined(CONFIG_IDF_TARGET_ESP32)
  // Normalize type
  toLowerCase(type);

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 16 * 1024;

  auto try_sdmmc = [&]() -> esp_err_t {
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;

    sdmmc_card_t *card = nullptr;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(sdMountPoint.c_str(), &host,
                                            &slot_config, &mount_config, &card);
    if (ret == ESP_OK) {
      sdCard = (void *)card;
      sdCardInitialized = true;
      config["sd"]["initialized"] = true;
    }
    return ret;
  };

  auto try_sdspi = [&]() -> esp_err_t {
    // Default pins commonly used on ESP32 display boards (VSPI):
    // SCLK=18, MISO=19, MOSI=23, CS=5
    int mosi = SDCARD_SPI_MOSI_GPIO_NUM;
    int miso = SDCARD_SPI_MISO_GPIO_NUM;
    int sclk = SDCARD_SPI_SCK_GPIO_NUM;
    int cs = SDCARD_SPI_CS_GPIO_NUM;
    int hostId = -1;

    JsonVariant sd = config["sd"];
    JsonVariant spi = sd["spi"];
    if (spi.is<JsonObject>()) {
      if (!spi["mosi"].isNull())
        mosi = spi["mosi"].as<int>();
      if (!spi["miso"].isNull())
        miso = spi["miso"].as<int>();
      if (!spi["sclk"].isNull())
        sclk = spi["sclk"].as<int>();
      if (!spi["cs"].isNull())
        cs = spi["cs"].as<int>();
      if (!spi["host"].isNull())
        hostId = spi["host"].as<int>();
    } else {
      // Allow flat keys: sd.mosi, sd.miso, sd.sclk, sd.cs
      if (!sd["mosi"].isNull())
        mosi = sd["mosi"].as<int>();
      if (!sd["miso"].isNull())
        miso = sd["miso"].as<int>();
      if (!sd["sclk"].isNull())
        sclk = sd["sclk"].as<int>();
      if (!sd["cs"].isNull())
        cs = sd["cs"].as<int>();
      if (!sd["host"].isNull())
        hostId = sd["host"].as<int>();
    }

    // If user explicitly provided invalid pins, bail out (avoid conflicts).
    if (cs < 0 || sclk < 0 || mosi < 0 || miso < 0) {
      sdInitNotConfiguredForTarget = true;
      return ESP_ERR_INVALID_ARG;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // Choose a default host bus if not provided
    if (hostId == 2 || hostId == 3) {
      host.slot = hostId;
    } else {
      host.slot = SDCARD_SPI_HOST;
    }
    sdSpiHost = host.slot;

    log(DEBUG, "\tSD(SPI) host: %d", host.slot);
    log(DEBUG, "\tSD(SPI) sclk: %d", sclk);
    log(DEBUG, "\tSD(SPI) miso: %d", miso);
    log(DEBUG, "\tSD(SPI) mosi: %d", mosi);
    log(DEBUG, "\tSD(SPI) cs: %d", cs);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = mosi;
    bus_cfg.miso_io_num = miso;
    bus_cfg.sclk_io_num = sclk;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;

    // Best-effort bus init: if already initialized by another driver (e.g.
    // LCD), proceed without owning/freeing it.
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg,
                                       SPI_DMA_CH_AUTO);
    if (ret == ESP_OK) {
      sdSpiBusOwned = true;
    } else if (ret == ESP_ERR_INVALID_STATE) {
      // Bus already initialized elsewhere.
      sdSpiBusOwned = false;
      ret = ESP_OK;
    }
    if (ret != ESP_OK) {
      return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)cs;
    slot_config.host_id = (spi_host_device_t)host.slot;

    sdmmc_card_t *card = nullptr;
    ret = esp_vfs_fat_sdspi_mount(sdMountPoint.c_str(), &host, &slot_config,
                                  &mount_config, &card);
    if (ret != ESP_OK) {
      if (sdSpiBusOwned) {
        (void)spi_bus_free((spi_host_device_t)host.slot);
        sdSpiBusOwned = false;
      }
      sdSpiHost = -1;
      return ret;
    }

    sdCard = (void *)card;
    sdCardInitialized = true;
    config["sd"]["initialized"] = true;
    return ESP_OK;
  };

  esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
  if (type == "sdspi" || type == "spi") {
    ret = try_sdspi();
  } else if (type == "sdmmc") {
    ret = try_sdmmc();
  } else {
    // auto: try SDMMC first, then SPI
    ret = try_sdmmc();
    if (ret != ESP_OK) {
      ret = try_sdspi();
    }
  }

  if (ret != ESP_OK) {
    sdCardInitialized = false;
    sdCard = nullptr;
    sdInitLastErr = ret;
    config["sd"]["initialized"] = false;
    log(WARNING, "💾 SD mount failed (%s): %s", type.c_str(),
        esp_err_to_name(ret));
    return;
  }

  log(INFO, "💾 SD mounted: %s", sdMountPoint.c_str());
#else
  // For non-ESP32 targets, SD wiring varies widely (SDSPI vs SDMMC, pin mux,
  // etc). Keep this explicit to avoid accidental pin conflicts.
  sdCardInitialized = false;
  sdCard = nullptr;
  sdInitNotConfiguredForTarget = true;
  sdInitLastErr = ESP_ERR_NOT_SUPPORTED;
  config["sd"]["initialized"] = false;
  log(WARNING, "💾 SD enabled but not supported on this target");
#endif
}

void ESPWiFi::sdCardConfigHandler() {
  // Config-gated SD mount/unmount. Keep this out of HTTP handlers.
  const bool enabled = config["sd"]["enabled"].isNull()
                           ? false
                           : config["sd"]["enabled"].as<bool>();

  if (!enabled && sdCardInitialized) {
    deinitSDCard();
  } else if (enabled && !sdCardInitialized) {
    initSDCard();
  }
}

void ESPWiFi::deinitSDCard() {
  if (!sdCardInitialized) {
    // Still ensure config reflects current state.
    config["sd"]["initialized"] = false;
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32)
  // Best-effort unmount. This can fail if card was never mounted correctly.
  sdmmc_card_t *card = (sdmmc_card_t *)sdCard;
  esp_vfs_fat_sdcard_unmount(sdMountPoint.c_str(), card);
  if (sdSpiBusOwned && sdSpiHost >= 0) {
    (void)spi_bus_free((spi_host_device_t)sdSpiHost);
  }
#endif

  sdCardInitialized = false;
  sdCard = nullptr;
  sdSpiBusOwned = false;
  sdSpiHost = -1;
  config["sd"]["initialized"] = false;
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

  if (fsParam == "lfs" && littleFsInitialized) {
    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info("littlefsp", &total, &used);
    if (ret == ESP_OK) {
      totalBytes = total;
      usedBytes = used;
      freeBytes = total - used;
    }
    return;
  }

  if (fsParam == "sd" && sdCardInitialized) {
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
  if (littleFsInitialized) {
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("LittleFS", totalBytes, usedBytes);
  }

  if (!config["sd"]["enabled"]) {
    return;
  }

  if (sdCardInitialized) {
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("sd", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("SD", totalBytes, usedBytes);
    return;
  }

  if (sdInitAttempted) {
    if (sdInitNotConfiguredForTarget) {
      log(WARNING,
          "💾 SD enabled in config (mountpoint %s)\n"
          "ESPWiFi does not currently support SD mounting for this device\n"
          "Disable (sd.enabled=false) in the config to avoid this message.",
          sdMountPoint.c_str());
    } else if (sdInitLastErr != ESP_OK) {
      log(WARNING, "💾 SD Card init failed: %s",
          esp_err_to_name(sdInitLastErr));
    } else {
      log(WARNING, "💾 SD Card enabled but not initialized");
    }
  } else {
    log(WARNING, "💾 SD Card enabled but init was not attempted");
  }
}

bool ESPWiFi::deleteDirectoryRecursive(const std::string &dirPath) {
  if (!littleFsInitialized)
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
      yield();
    }
  }
  closedir(dir);

  std::string full_dir_path = lfsMountPoint + dirPath;
  return ::rmdir(full_dir_path.c_str()) == 0;
}

bool ESPWiFi::writeFile(const std::string &filePath, const uint8_t *data,
                        size_t len) {
  if (!littleFsInitialized)
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
  yield(); // Yield after file write

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
  if (!littleFsInitialized) {
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
    return filename.substr(pos + 1);
  }
  return "";
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
