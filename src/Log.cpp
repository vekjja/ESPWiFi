#ifndef ESPWiFi_LOG
#define ESPWiFi_LOG

// Platform-specific includes
#ifdef ESP8266
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#elif defined(ESP32)
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#endif

#include "ESPWiFi.h"
#include <stdarg.h>

// Global file handle for logging
static File logFileHandle;

void ESPWiFi::startSerial(int baudRate) {
  if (Serial) {
    return;
  }
  Serial.begin(baudRate);
  Serial.setDebugOutput(true);
  delay(999); // wait for serial to start
  log("⛓️  Serial Started:");
  logf("\tBaud: %d\n", baudRate);
}

void ESPWiFi::startLogging(String filePath) {
  if (loggingStarted) {
    return;
  }

  startSerial();
  initLittleFS();
  initSDCard();
  this->logFilePath = filePath;

  closeLog(); // Close any existing log file

  if (!fs) {
    logError("No file system available for logging");
    return;
  }

  logFileHandle = fs->open(logFilePath, "a");
  if (!logFileHandle) {
    logError("Failed to open log file");
    return;
  }

  loggingStarted = true;
  log("📝 Logging started:");
  logf("\tFile Name: %s\n", logFilePath.c_str());
  logf("\tFile System: %s\n", sdCardInitialized ? "SD Card" : "LittleFS");
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::checkAndCleanupLogFile() {
  if (logFileHandle) {
    size_t logFileSize = logFileHandle.size();

    // 600KB = 614400 bytes
    const size_t MAX_LOG_FILE_SIZE = 614400;

    if (logFileSize > MAX_LOG_FILE_SIZE) {
      logFileHandle.close();

      // Delete the log file to free up space
      if (sdCardInitialized) {
        if (SD.remove(logFilePath)) {
          log("🗑️  Log file deleted to free up space");
        } else {
          logError("Failed to delete log file");
        }
#ifdef ESP8266
        logFileHandle = SD.open(logFilePath, FILE_WRITE);
#elif defined(ESP32)
        logFileHandle = SD.open(logFilePath, FILE_APPEND);
#endif
      } else {
        if (LittleFS.remove(logFilePath)) {
          log("🗑️  Log file deleted to free up space");
        } else {
          logError("Failed to delete log file");
        }
        logFileHandle = LittleFS.open(logFilePath, "a");
      }
      if (logFileHandle) {
        log("📝 New log file created");
      }
    }
  }
}

void ESPWiFi::writeLog(String message) {
  if (logFileHandle) {
    logFileHandle.print(message);
    logFileHandle.flush(); // Ensure data is written immediately
  }
}

void ESPWiFi::logError(String message) {
  String errMsg = "❗️ Error: " + message;
  log(errMsg);
}

String ESPWiFi::timestamp() {
  unsigned long milliseconds = millis();
  unsigned long seconds = milliseconds / 1000;
  unsigned long days = seconds / 86400;
  unsigned long minutes = (seconds % 86400) / 60;
  seconds = seconds % 60;
  milliseconds = milliseconds % 1000;

  return "[" + String(days) + ":" + String(minutes) + ":" + String(seconds) +
         ":" + String(milliseconds) + "] ";
}

String ESPWiFi::timestampForFilename() {
  unsigned long milliseconds = millis();
  unsigned long seconds = milliseconds / 1000;
  unsigned long days = seconds / 86400;
  unsigned long minutes = (seconds % 86400) / 60;
  seconds = seconds % 60;
  milliseconds = milliseconds % 1000;

  return String(days) + "_" + String(minutes) + "_" + String(seconds) + "_" +
         String(milliseconds);
}

void ESPWiFi::log(String message) {
  String ts = timestamp();
  Serial.println(ts + message);
  Serial.flush(); // Ensure immediate output
  writeLog(ts + message + "\n");
}

void ESPWiFi::logf(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  String ts = timestamp();
  Serial.print(ts + buffer);
  Serial.flush(); // Ensure immediate output
  writeLog(ts + buffer);
}

// Add a method to close the log file (call this in setup or when needed)
void ESPWiFi::closeLog() {
  if (logFileHandle) {
    logFileHandle.close();
  }
}

#endif // ESPWiFi_LOG
