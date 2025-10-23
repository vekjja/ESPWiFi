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
  if (serialStarted) {
    return;
  }
  Serial.begin(baudRate);
  Serial.setDebugOutput(true);
  serialStarted = true;
  delay(999); // wait for serial to start
  log("⛓️  Serial Started:");
  logf("\tBaud: %d\n", baudRate);
}

void ESPWiFi::startLogging(String filePath) {
  if (loggingStarted) {
    return;
  }

  if (!serialStarted) {
    startSerial();
  }

  loggingStarted = true; // Set this BEFORE calling initSDCard()
  initLittleFS();
  initSDCard();
  this->logFilePath = filePath;

  closeLogFile();
  openLogFile();
  cleanLogFile();
  log("📝 Logging started:");
  logf("\tFile Name: %s\n", logFilePath.c_str());
  logf("\tFile System: %s\n", sdCardInitialized ? "SD Card" : "LittleFS");
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::cleanLogFile() {
  if (logFileHandle) {
    size_t logFileSize = logFileHandle.size();
    if (logFileSize > maxLogFileSize) {
      closeLogFile();
      // Delete the log file to free up space using the fs member
      if (fs && fs->remove(logFilePath)) {
        log("🗑️  Log file deleted to free up space");
      } else {
        logError("Failed to delete log file");
      }
      openLogFile();
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
  if (!serialStarted) {
    startSerial();
  }
  if (!loggingStarted) {
    startLogging();
  }
  String ts = timestamp();
  Serial.println(ts + message);
  Serial.flush(); // Ensure immediate output
  writeLog(ts + message + "\n");
}

void ESPWiFi::logf(const char *format, ...) {
  if (!serialStarted) {
    startSerial();
  }
  if (!loggingStarted) {
    startLogging();
  }

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
void ESPWiFi::closeLogFile() {
  if (logFileHandle) {
    logFileHandle.close();
  }
}

void ESPWiFi::openLogFile() {
  if (!fs) {
    logError("No file system available for logging");
    return;
  }

  logFileHandle = fs->open(logFilePath, "a");
  if (!logFileHandle) {
    logError("Failed to open log file");
    return;
  }
}

void ESPWiFi::srvLog() {
  initWebServer();
  webServer->on("/log", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (fs && fs->exists(logFilePath)) {
      AsyncWebServerResponse *response = request->beginResponse(
          *fs, logFilePath,
          "text/plain; charset=utf-8"); // Set UTF-8 encoding
      addCORS(response);
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/plain; charset=utf-8",
                                 "Log file not found"); // Set UTF-8 encoding
      addCORS(response);
      request->send(response);
    }
  });
}
#endif // ESPWiFi_LOG
