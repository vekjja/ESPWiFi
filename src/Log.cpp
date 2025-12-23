#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

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
  delay(1500); // wait for serial to start
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

  loggingStarted = true;

  initLittleFS();
  initSDCard();
  this->logFilePath = filePath;

  openLogFile();
  cleanLogFile();

  logf("\n\n%s🌌 FirmaMint %s\n\n", timestamp().c_str(), version.c_str());

  if (Serial) {
    logln("📺 Serial Output Enabled");
    logInfof("\tBaud: %d\n", baudRate);
  }

  printFilesystemInfo();
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::cleanLogFile() {
  if (logFile) {
    size_t logFileSize = logFile.size();
    if (logFileSize > maxLogFileSize) {
      closeLogFile();
      bool deleted = false;
      if (sdCardInitialized && sd) {
        deleted = sd->remove(logFilePath);
      } else if (lfs) {
        deleted = lfs->remove(logFilePath);
      }

      if (deleted) {
        logln("🗑️  Log file " + logFilePath + " refreshed on " +
              (sdCardInitialized ? "SD Card" : "Internal Storage"));
      } else {
        logError("Failed to delete log file");
      }
      openLogFile();
    }
  }
}

void ESPWiFi::writeLog(String message) {
  // Check if log file is valid, if not try to recreate it
  if (!logFile) {
    openLogFile();
  }

  if (logFile) {
    logFile.print(message);
    logFile.flush(); // Ensure data is written immediately
  }
}

bool ESPWiFi::shouldLog(String level) {
  // Check if logging is enabled
  if (!config["log"]["enabled"].as<bool>()) {
    return false;
  }

  String configuredLevel = config["log"]["level"].as<String>();

  // Convert to lowercase for case-insensitive comparison
  configuredLevel.toLowerCase();
  level.toLowerCase();

  // Hierarchy: debug > info > warning > error
  // debug: shows debug, info, warn, error
  // info: shows info, warn, error
  // warning: shows warn, error
  // error: shows error only

  if (configuredLevel == "debug") {
    return true; // Show all levels
  } else if (configuredLevel == "info") {
    return (level == "info" || level == "warning" || level == "warn" ||
            level == "error");
  } else if (configuredLevel == "warning" || configuredLevel == "warn") {
    return (level == "warning" || level == "warn" || level == "error");
  } else if (configuredLevel == "error") {
    return (level == "error");
  }

  // Default: if level is not recognized, allow it (backward compatibility)
  return true;
}

String ESPWiFi::formatLog(const char *format, va_list args) {
  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), format, args);
  return String(buffer);
}

void ESPWiFi::log(String message) {
  if (!serialStarted) {
    startSerial();
  }
  if (!loggingStarted) {
    startLogging();
  }
  String ts = timestamp();
  Serial.print(ts + message);
  Serial.flush(); // Ensure immediate output
  writeLog(ts + message);
}
void ESPWiFi::logln(String message) { log(message + "\n"); }

void ESPWiFi::logDebug(String message) {
  if (!shouldLog("debug")) {
    return;
  }
  log("🐛 Debug: " + message);
}

void ESPWiFi::logDebugf(const char *format, ...) {
  if (!shouldLog("debug")) {
    return;
  }
  va_list args;
  va_start(args, format);
  String formatted = formatLog(format, args);
  va_end(args);
  log("🐛 Debug: " + formatted);
}

void ESPWiFi::logInfo(String message) {
  if (!shouldLog("info")) {
    return;
  }
  log(message);
}

void ESPWiFi::logInfof(const char *format, ...) {
  if (!shouldLog("info")) {
    return;
  }
  va_list args;
  va_start(args, format);
  String formatted = formatLog(format, args);
  va_end(args);
  log(formatted.c_str());
}

void ESPWiFi::logWarn(String message) {
  if (!shouldLog("warning")) {
    return;
  }
  log("⚠️  Warning: " + message);
}

void ESPWiFi::logWarnf(const char *format, ...) {
  if (!shouldLog("warning")) {
    return;
  }
  va_list args;
  va_start(args, format);
  String formatted = formatLog(format, args);
  va_end(args);
  log("⚠️  Warning: " + formatted);
}

void ESPWiFi::logError(String message) {
  if (!shouldLog("error")) {
    return;
  }
  log("❗️ Error: " + message);
}

void ESPWiFi::logErrorf(const char *format, ...) {
  if (!shouldLog("error")) {
    return;
  }
  va_list args;
  va_start(args, format);
  String formatted = formatLog(format, args);
  va_end(args);
  log("❗️ Error: " + formatted);
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

void ESPWiFi::logf(const char *format, ...) {
  // Default logf() function uses info level - format and call log() directly
  if (!shouldLog("info")) {
    return;
  }
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  log(String(buffer));
}

void ESPWiFi::closeLogFile() {
  if (logFile) {
    logFile.close();
  }
}

void ESPWiFi::openLogFile() {
  // Try SD card first, fallback to LittleFS for logging
  if (sdCardInitialized && sd) {
    logFile = sd->open(logFilePath, "a");
  } else if (lfs) {
    logFile = lfs->open(logFilePath, "a");
  } else {
    logError("No file system available for logging");
    return;
  }

  if (!logFile) {
    logError("Failed to open log file");
  }
}

void ESPWiFi::srvLog() {
  initWebServer();

  // CORS preflight
  webServer->on("/log", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    handleCorsPreflight(request);
  });

  // GET /log - return log file content
  webServer->on("/log", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (!authorized(request)) {
      sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
      return;
    }

    // Try SD card first, then LittleFS
    FS *filesystem = nullptr;
    if (sdCardInitialized && sd && sd->exists(logFilePath)) {
      filesystem = sd;
    } else if (lfs && lfs->exists(logFilePath)) {
      filesystem = lfs;
    }

    if (!filesystem) {
      sendJsonResponse(request, 404, "{\"error\":\"Log file not found\"}");
      return;
    }

    // Use the filesystem response method for efficient streaming
    AsyncWebServerResponse *response =
        request->beginResponse(*filesystem, logFilePath, "text/plain");
    addCORS(response);
    response->addHeader("Content-Type", "text/plain; charset=utf-8");
    request->send(response);
  });
}

void ESPWiFi::logConfigHandler() {
  static bool lastEnabled = true;
  static String lastLevel = "info";

  bool currentEnabled = config["log"]["enabled"].as<bool>();
  String currentLevel = config["log"]["level"].as<String>();

  // Log when enabled state changes
  if (currentEnabled != lastEnabled) {
    logf("📝 Logging %s\n", currentEnabled ? "enabled" : "disabled");
    lastEnabled = currentEnabled;
  }

  // Log when level changes
  if (currentLevel != lastLevel) {
    logf("📝 Log level changed: %s -> %s\n", lastLevel.c_str(),
         currentLevel.c_str());
    lastLevel = currentLevel;
  }
}
