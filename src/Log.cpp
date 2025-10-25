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

void ESPWiFi::startSerial(int baudRate) {
  if (serialStarted) {
    return;
  }
  this->baudRate = baudRate;
  Serial.begin(baudRate);
  Serial.setDebugOutput(true);
  serialStarted = true;
  delay(999); // wait for serial to start
  Serial.println(timestamp() + "⛓️  Serial Started:");
  Serial.printf("%s\tBaud: %d\n", timestamp().c_str(), baudRate);
}

void ESPWiFi::startLogging(String filePath) {
  if (loggingStarted) {
    return;
  }

  if (!serialStarted) {
    startSerial();
  }

  // This must be set before calling initSDCard() to prevent circular calls
  loggingStarted = true;

  initLittleFS();
  initSDCard();
  this->logFilePath = filePath;

  openLogFile();
  cleanLogFile();

  logf("\n\n%s🌌 FirmaMint v0.1.0\n\n", timestamp().c_str());

  if (Serial) {
    log("📺 Serial Output Enabled");
    logf("\tBaud: %d\n", baudRate);
  }

  printFilesystemInfo();

  log("📝 Logging started:");
  logf("\tFile Name: %s\n", logFilePath.c_str());
  logf("\tFile System: %s\n",
       (sdCardInitialized && sd) ? "SD Card" : "LittleFS");
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::cleanLogFile() {
  if (logFile) {
    size_t logFileSize = logFile.size();
    if (logFileSize > maxLogFileSize) {
      closeLogFile();
      // Delete the log file to free up space using the tracked filesystem
      bool deleted = false;
      if (logFileSystem) {
        deleted = logFileSystem->remove(logFilePath);
      }

      if (deleted) {
        log("🗑️  Log file deleted to free up space");
      } else {
        logError("Failed to delete log file");
      }
      openLogFile();
    }
  }
}

void ESPWiFi::writeLog(String message) {
  if (logFile) {
    logFile.print(message);
    logFile.flush(); // Ensure data is written immediately
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

void ESPWiFi::closeLogFile() {
  if (logFile) {
    logFile.close();
  }
  logFileSystem = nullptr;
}

void ESPWiFi::openLogFile() {
  // Try SD card first, fallback to LittleFS for logging
  if (sdCardInitialized && sd) {
    logFile = sd->open(logFilePath, "a");
    logFileSystem = sd;
  } else if (lfs) {
    logFile = lfs->open(logFilePath, "a");
    logFileSystem = lfs;
  } else {
    logError("No file system available for logging");
    logFileSystem = nullptr;
    return;
  }

  if (!logFile) {
    logError("Failed to open log file");
    logFileSystem = nullptr;
  }
}

void ESPWiFi::srvLog() {
  initWebServer();
  webServer->on("/log", HTTP_GET, [this](AsyncWebServerRequest *request) {
    // Use the current logFile to serve the active log from the correct
    // filesystem
    if (logFile && logFileSystem) {
      if (logFileSystem->exists(logFilePath)) {
        AsyncWebServerResponse *response = request->beginResponse(
            *logFileSystem, logFilePath,
            "text/plain; charset=utf-8"); // Set UTF-8 encoding
        addCORS(response);
        request->send(response);
      } else {
        AsyncWebServerResponse *response = request->beginResponse(
            404, "text/plain; charset=utf-8",
            "Log file not found on filesystem"); // Set UTF-8 encoding
        addCORS(response);
        request->send(response);
      }
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse(404, "text/plain; charset=utf-8",
                                 "No active log file"); // Set UTF-8 encoding
      addCORS(response);
      request->send(response);
    }
  });
}
#endif // ESPWiFi_LOG
