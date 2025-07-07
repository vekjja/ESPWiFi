#ifndef ESPWiFi_FileSystem
#define ESPWiFi_FileSystem
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

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

// void ESPWiFi::startSDCard() {
//   if (sdCardStarted) {
//     return;
//   }

//   // Initialize SD card
//   if (!SD.begin()) {
//     logError("Failed to mount SD card");
//     return;
//   }

//   fs = &SD;
//   sdCardStarted = true;
//   log("💾 SD Card Started:");

//   size_t totalBytes = SD.totalBytes();
//   size_t usedBytes = SD.usedBytes();

//   logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
//   logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
//   logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
// }

void ESPWiFi::listFiles(FS &fs) {
  log("📂 Listing Files:");
  File root = fs.open("/", "r");
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
  File file = fs.open(filePath, "r");
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
  File file = fs.open(filePath, "w");
  if (!file) {
    logError("Failed to open file for writing: " + filePath);
    return;
  }

  file.print(data);
  file.close();
  logf("📂 Written to file: %s", filePath.c_str());
}

void ESPWiFi::appendToFile(FS &fs, const String &filePath, const String &data) {
  File file = fs.open(filePath, "a");
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

#endif // ESPWiFi_FileSystem