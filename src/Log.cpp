#ifndef ESPWiFi_LOG
#define ESPWiFi_LOG

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
    log("📺 Serial Output Enabled");
    logf("\tBaud: %d\n", baudRate);
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
        log("🗑️  Log file " + logFilePath + " deleted to free up space");
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

  // PUT /api/log - update log settings (enabled, level)
  webServer->on(
      "/api/log", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/api/log", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }

        if (request->method() != HTTP_PUT) {
          sendJsonResponse(request, 405, "{\"error\":\"Method not allowed\"}");
          return;
        }

        JsonObject reqJson = json.as<JsonObject>();

        if (reqJson["enabled"].is<bool>()) {
          config["log"]["enabled"] = reqJson["enabled"].as<bool>();
        }

        if (reqJson["level"].is<const char *>()) {
          String level = reqJson["level"].as<String>();
          level.toLowerCase();
          if (level == "debug" || level == "info" || level == "warning" ||
              level == "error") {
            config["log"]["level"] = level;
          } else {
            sendJsonResponse(request, 400,
                             "{\"error\":\"Invalid log level. Must be: debug, "
                             "info, warning, or error\"}");
            return;
          }
        }

        saveConfig();

        JsonDocument responseDoc;
        responseDoc["enabled"] = config["log"]["enabled"].as<bool>();
        responseDoc["level"] = config["log"]["level"].as<String>();
        responseDoc["success"] = true;

        String jsonResponse;
        serializeJson(responseDoc, jsonResponse);
        sendJsonResponse(request, 200, jsonResponse);
      }));
}

#endif // ESPWiFi_LOG
