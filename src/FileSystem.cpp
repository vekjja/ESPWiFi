#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem

#include "ESPWiFi.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
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
  // SD Card support commented out for now
  sdCardInitialized = false;
  log(WARNING, "💾 SD Card not implemented yet");
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
  }
}

void ESPWiFi::logFilesystemInfo(const std::string &fsName, size_t totalBytes,
                                size_t usedBytes) {
  log(INFO, "💾 %s Filesystem", fsName.c_str());
  log(DEBUG, "\tTotal: %s", bytesToHumanReadable(totalBytes).c_str());
  log(DEBUG, "\tUsed: %s", bytesToHumanReadable(usedBytes).c_str());
  log(DEBUG, "\tFree: %s",
      bytesToHumanReadable(totalBytes - usedBytes).c_str());
}

void ESPWiFi::printFilesystemInfo() {
  if (littleFsInitialized) {
    size_t totalBytes, usedBytes, freeBytes;
    getStorageInfo("lfs", totalBytes, usedBytes, freeBytes);
    logFilesystemInfo("LittleFS", totalBytes, usedBytes);
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

  std::string full_path = lfsMountPoint + filePath;
  FILE *file = fopen(full_path.c_str(), "wb");
  if (!file)
    return false;

  size_t written = fwrite(data, 1, len, file);
  // Yield after file I/O to prevent watchdog timeout
  if (len > 1024) {
    yield();
  }
  fclose(file);

  return written == len;
}

bool ESPWiFi::writeFileAtomic(const std::string &filePath, const uint8_t *data,
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

bool ESPWiFi::isRestrictedSystemFile(const std::string &fsParam,
                                     const std::string &filePath) {
  // Only restrict files on LittleFS, not SD card
  if (fsParam != "lfs") {
    return false;
  }

  // List of restricted system files and directories
  if (filePath == "/config.json" || filePath == "/index.html" ||
      filePath == "/asset-manifest.json" || filePath == "/dashboard.zip" ||
      filePath.find("/static/") == 0 || filePath.find("/system/") == 0 ||
      filePath.find("/boot/") == 0) {
    return true;
  }

  // Also protect config file and log file
  if (filePath == configFile || filePath == logFilePath) {
    return true;
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
