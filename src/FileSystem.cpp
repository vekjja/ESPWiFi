#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

#include "ESPWiFi.h"

void ESPWiFi::startLittleFS() {
  if (littleFsEnabled) {
    return;
  }
  if (!LittleFS.begin()) {
    logError("  Failed to mount LittleFS");
    return;
  }
  littleFsEnabled = true;
  log("💾 LittleFS mounted:");
#if CONFIG_IDF_TARGET_ESP8266
  FSInfo fs_info;
  LittleFS.info(fs_info);
  size_t totalBytes = fs_info.totalBytes;
  size_t usedBytes = fs_info.usedBytes;
#elif CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
#endif
  logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
  logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
  logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
}

void ESPWiFi::startSDCard() {
  if (sdCardEnabled) {
    return;
  }
#if CONFIG_IDF_TARGET_ESP32S3
  if (!SD.begin(21)) {
#else
  if (!SD.begin()) {
#endif
    logError("Failed to initialize SD card");
    return;
  }
  sdCardEnabled = true;
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  log("📂 SD Card initialized:");
  logf("\tCard Type: %s", SD.cardType());
  logf("\tTotal Size: %s", bytesToHumanReadable(totalBytes).c_str());
  logf("\tUsed Size: %s", bytesToHumanReadable(usedBytes).c_str());
  logf("\tFree Size: %s", bytesToHumanReadable(totalBytes - usedBytes).c_str());
}

void ESPWiFi::listFiles(FS &fs) {
  log("📂 Listing Files:");
  File root = fs.open("/");
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

void ESPWiFi::readFile(FS &fs, const String &filePath) {
  File file = fs.open(filePath);
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

void ESPWiFi::writeFile(FS &fs, const String &filePath, const String &data) {
  File file = fs.open(filePath, FILE_WRITE);
  if (!file) {
    logError("Failed to open file for writing: " + filePath);
    return;
  }

  file.print(data);
  file.close();
  logf("📂 Written to file: %s", filePath.c_str());
}

void ESPWiFi::appendToFile(FS &fs, const String &filePath, const String &data) {
  File file = fs.open(filePath, FILE_APPEND);
  if (!file) {
    logError("Failed to open file for appending: " + filePath);
    return;
  }

  file.print(data);
  file.close();
  logf("📂 Appended to file: %s", filePath.c_str());
}

void ESPWiFi::deleteFile(FS &fs, const String &filePath) {
  if (fs.remove(filePath)) {
    logf("📂 Deleted file: %s", filePath.c_str());
  } else {
    logError("Failed to delete file: " + filePath);
  }
}

#endif  // ESPWiFi_FileSystem