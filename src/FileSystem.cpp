#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem
#include <LittleFS.h>
#include <SD.h>
#include <SD_MMC.h>

#include "ESPWiFi.h"

void ESPWiFi::startLittleFS() {
  if (littleFsStarted) {
    return;
  }
  if (!LittleFS.begin()) {
    logError("  Failed to mount LittleFS");
    return;
  }

  fs = &LittleFS;
  littleFsStarted = true;
  log("💾 LittleFS Started:");

  size_t totalBytes, usedBytes;

#ifdef ESP8266
  FSInfo fs_info;
  LittleFS.info(fs_info);
  totalBytes = fs_info.totalBytes;
  usedBytes = fs_info.usedBytes;
#elif defined(ESP32)
  totalBytes = LittleFS.totalBytes();
  usedBytes = LittleFS.usedBytes();
#endif

  logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
  logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
  logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
}

void ESPWiFi::startSDCard() {
  if (sdCardStarted) {
    return;
  }

#if defined(CONFIG_IDF_TARGET_ESP32) // ESP32-CAM
  if (!SD_MMC.begin()) {
    log("⚠️  Failed to mount SD card");
    return;
  }
  fs = &SD_MMC;
  size_t totalBytes = SD_MMC.totalBytes();
  size_t usedBytes = SD_MMC.usedBytes();
#else // ESP32-S2, ESP32-S3, ESP32-C3

#if defined(CONFIG_IDF_TARGET_ESP32S3) // ESP32-S3
  int sdCardPin = 21;
#endif

  if (!SD.begin(sdCardPin)) {
    log("⚠️  Failed to mount SD card");
    return;
  }
  fs = &SD;

  size_t totalBytes = SD.totalBytes();
  size_t usedBytes = SD.usedBytes();
#endif

  sdCardStarted = true;
  log("💾 SD Card Started:");

  logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
  logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
  logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
}

void ESPWiFi::listFiles(FS *fs) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  log("📂 Listing Files:");
  File root = fs->open("/", "r");
  if (!root) {
    logError("Failed to open root directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    logf("\t%s (%s)", file.name(), bytesToHumanReadable(file.size()).c_str());
    file = root.openNextFile();
  }

  root.close();
}

void ESPWiFi::readFile(FS *fs, const String &filePath) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  File file = fs->open(filePath, "r");
  if (!file) {
    logError("Failed to open file: " + filePath);
    return;
  }

  logf("📂 Reading file: %s", filePath.c_str());
  while (file.available()) {
    Serial.write(file.read());
  }

  file.close();
}

void ESPWiFi::writeFile(FS *fs, const String &filePath, const String &data) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  File file = fs->open(filePath, "w");
  if (!file) {
    logError("Failed to open file for writing: " + filePath);
    return;
  }

  file.print(data);
  file.close();
  logf("📂 Written to file: %s", filePath.c_str());
}

void ESPWiFi::appendToFile(FS *fs, const String &filePath, const String &data) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  File file = fs->open(filePath, "a");
  if (!file) {
    logError("Failed to open file for appending: " + filePath);
    return;
  }

  file.print(data);
  file.close();
  logf("📂 Appended to file: %s", filePath.c_str());
}

void ESPWiFi::deleteFile(FS *fs, const String &filePath) {
  if (!fs) {
    logError("File system is null");
    return;
  }

  if (fs->remove(filePath)) {
    logf("📂 Deleted file: %s", filePath.c_str());
  } else {
    logError("Failed to delete file: " + filePath);
  }
}

bool ESPWiFi::fileExists(FS *fs, const String &filePath) {
  if (!fs) {
    logError("File system is null");
    return false;
  }

  File file = fs->open(filePath, "r");
  if (file) {
    file.close();
    return true;
  }
  return false;
}

bool ESPWiFi::dirExists(FS *fs, const String &dirPath) {
  if (!fs) {
    logError("File system is null");
    return false;
  }

  File dir = fs->open(dirPath, "r");
  if (dir) {
    bool isDir = dir.isDirectory();
    dir.close();
    return isDir;
  }
  return false;
}

bool ESPWiFi::mkDir(FS *fs, const String &dirPath) {
  if (!fs) {
    logError("File system is null");
    return false;
  }

  if (dirExists(fs, dirPath)) {
    logf("📁 Directory already exists: %s", dirPath.c_str());
    return true;
  }

  if (fs->mkdir(dirPath)) {
    logf("📁 Created directory: %s", dirPath.c_str());
    return true;
  } else {
    logError("Failed to create directory: " + dirPath);
    return false;
  }
}

#endif // ESPWiFi_FileSystem