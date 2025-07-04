#ifndef ESPWIFI_LOG
#define ESPWIFI_LOG

#include <stdarg.h>

#include "ESPWiFi.h"

// Global file handle for logging
static File logFileHandle;

void ESPWiFi::startSerial(int baudRate = 115200) {
  if (Serial) {
    return;
  }
  Serial.begin(baudRate);
  Serial.setDebugOutput(true);
  delay(999);  // wait for serial to start
  logf("⛓️  Serial Started:\n\tBaud: %d\n", baudRate);
}

void ESPWiFi::startLog(String logFile) {
  startSerial();
  startLittleFS();
  this->logFile = logFile;
  log("📝 Logging started:");
  logf("\tFile: %s\n", logFile.c_str());
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::checkAndCleanupLogFile() {
#ifdef ESP8266
  FSInfo fs_info;
  LittleFS.info(fs_info);
  size_t totalBytes = fs_info.totalBytes;
  size_t usedBytes = fs_info.usedBytes;
#elif defined(ESP32)
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
#endif
  size_t freeBytes = totalBytes - usedBytes;

  // 100KB = 102400 bytes
  const size_t MIN_FREE_SPACE = 102400;

  if (freeBytes < MIN_FREE_SPACE) {
    logError("Low filesystem space detected:");
    logf("\tFree space: %s", bytesToHumanReadable(freeBytes).c_str());
    logf("\tMinimum required: %s",
         bytesToHumanReadable(MIN_FREE_SPACE).c_str());

    // Close log file if it's open
    if (logFileHandle) {
      logFileHandle.close();
    }

    // Delete the log file to free up space
    if (LittleFS.remove(logFile)) {
      log("🗑️  Log file deleted to free up space");
    } else {
      logError("Failed to delete log file");
    }

    // Reopen log file for new entries
    logFileHandle = LittleFS.open(logFile, "a");
    if (logFileHandle) {
      log("📝 New log file created");
    }
  }
}

void ESPWiFi::writeLog(String message) {
  if (logFileHandle) {
    checkAndCleanupLogFile();
    logFileHandle.print(message);
    logFileHandle.flush();  // Ensure data is written immediately
  } else {
    startLittleFS();
    logFileHandle = LittleFS.open(logFile, "a");
    writeLog(message);
  }
}

void ESPWiFi::logError(String message) {
  String errMsg = "💔 Error: " + message;
  log(errMsg);
}

void ESPWiFi::log(String message) {
  Serial.println(message);
  Serial.flush();  // Ensure immediate output
  writeLog(message + "\n");
}

void ESPWiFi::logf(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.print(buffer);
  Serial.flush();
  writeLog(buffer);
}

// Add a method to close the log file (call this in setup or when needed)
void ESPWiFi::closeLog() {
  if (logFileHandle) {
    logFileHandle.close();
  }
}

#endif  // ESPWIFI_LOG
