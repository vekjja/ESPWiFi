#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem

#include "ESPWiFi.h"
#include "esp_littlefs.h"
#include <dirent.h>
#include <sys/stat.h>

void ESPWiFi::initLittleFS() {
  if (littleFsInitialized) {
    return;
  }

  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs",
      .partition_label = "littlefs",
      .format_if_mount_failed = true,
      .dont_mount = false,
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

  lfs = new FS("/littlefs");
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
    esp_err_t ret = esp_littlefs_info("littlefs", &total, &used);
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

bool ESPWiFi::fileExists(FS *fs, const std::string &filePath) {
  if (!fs)
    return false;
  return fs->exists(filePath);
}

bool ESPWiFi::dirExists(FS *fs, const std::string &dirPath) {
  if (!fs)
    return false;
  std::string full_path = fs->mount_point + dirPath;
  struct stat st;
  if (stat(full_path.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return false;
}

bool ESPWiFi::mkDir(FS *fs, const std::string &dirPath) {
  if (!fs)
    return false;
  return fs->mkdir(dirPath);
}

bool ESPWiFi::deleteDirectoryRecursive(FS *fs, const std::string &dirPath) {
  if (!fs)
    return false;

  std::string full_path = fs->mount_point + dirPath;
  DIR *dir = opendir(full_path.c_str());
  if (!dir)
    return false;

  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string entry_path = dirPath + "/" + entry->d_name;
    std::string full_entry_path = fs->mount_point + entry_path;

    struct stat st;
    if (stat(full_entry_path.c_str(), &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        deleteDirectoryRecursive(fs, entry_path);
      } else {
        fs->remove(entry_path);
      }
    }
  }
  closedir(dir);

  return fs->rmdir(dirPath);
}

bool ESPWiFi::writeFile(FS *filesystem, const std::string &filePath,
                        const uint8_t *data, size_t len) {
  if (!filesystem)
    return false;

  File file = filesystem->open(filePath, "w");
  if (!file)
    return false;

  size_t written = file.write(data, len);
  file.close();

  return written == len;
}

bool ESPWiFi::isRestrictedSystemFile(const std::string &fsParam,
                                     const std::string &filePath) {
  // Protect config file and log file
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

// Stub for file upload handler
void ESPWiFi::handleFileUpload(void *req, const std::string &filename,
                               size_t index, uint8_t *data, size_t len,
                               bool final) {
  // Will implement later with HTTP server
}

#endif // ESPWiFi_FileSystem
