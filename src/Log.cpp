#ifndef ESPWIFI_LOG
#define ESPWIFI_LOG

#include "ESPWiFi.h"
#include <stdarg.h>

// Global file handle for logging
static File logFileHandle;

// Function to check filesystem space and delete log if needed
void ESPWiFi::checkAndCleanupLogFile() {

  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  size_t freeBytes = totalBytes - usedBytes;

  // 100KB = 102400 bytes
  const size_t MIN_FREE_SPACE = 102400;

  if (freeBytes < MIN_FREE_SPACE) {
    log("⚠️  Low filesystem space detected:");
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
      log("❌  Failed to delete log file");
    }

    // Reopen log file for new entries
    logFileHandle = LittleFS.open(logFile, "a");
    if (logFileHandle) {
      log("✅  New log file created");
    }
  }
}

void ESPWiFi::writeLog(String message) {
  if (logFileHandle) {
    checkAndCleanupLogFile();
    logFileHandle.print(message);
    logFileHandle.flush(); // Ensure data is written immediately
  } else {
    startLittleFS();
    logFileHandle = LittleFS.open(logFile, "a");
    writeLog(message);
  }
}

void ESPWiFi::log(String message) {
  Serial.println(message);
  writeLog(message + "\n");
}

void ESPWiFi::logf(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.print(buffer);
  writeLog(buffer);
}

// Add a method to close the log file (call this in setup or when needed)
void ESPWiFi::closeLog() {
  if (logFileHandle) {
    logFileHandle.close();
  }
}

#endif // ESPWIFI_LOG
