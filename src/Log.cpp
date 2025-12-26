#include "ESPWiFi.h"
#include "esp_timer.h"
#include <stdarg.h>
#include <sys/time.h>

void ESPWiFi::startSerial(int baudRate) {
  if (serialStarted) {
    return;
  }
  this->baudRate = baudRate;
  // ESP-IDF: Serial is already initialized by default via UART
  // Just mark it as started
  serialStarted = true;
  vTaskDelay(pdMS_TO_TICKS(100)); // Small delay for serial to stabilize
  printf("%s📺  Serial Started:\n", timestamp().c_str());
  printf("%s\tBaud: %d\n", timestamp().c_str(), baudRate);
}

void ESPWiFi::startLogging(std::string filePath) {
  if (loggingStarted) {
    return;
  }

  if (!serialStarted) {
    startSerial();
  }

  loggingStarted = true;

  initLittleFS();
  // initSDCard();  // Commented out for now
  this->logFilePath = filePath;

  openLogFile();
  cleanLogFile();

  log(INFO, "🌌 ESPWiFi Version: %s", version.c_str());

  if (serialStarted) {
    log(INFO, "📺 Serial Output Enabled");
    log(DEBUG, "\tBaud: %d", baudRate);
  }

  printFilesystemInfo();
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::cleanLogFile() {
  if (logFile && maxLogFileSize > 0) {
    size_t logFileSize = logFile.size();
    if (logFileSize > (size_t)maxLogFileSize) {
      closeLogFile();
      bool deleted = false;
      // if (sdCardInitialized && sd) {
      //   deleted = sd->remove(logFilePath);
      // } else
      if (lfs) {
        deleted = lfs->remove(logFilePath);
      }

      if (!deleted) {
        log(ERROR, "Failed to delete log file");
      }
      openLogFile();
    }
  }
}

void ESPWiFi::writeLog(std::string message) {
  // Check if log file is valid, if not try to recreate it
  if (!logFile) {
    openLogFile();
  }

  if (logFile && logFile.handle) {
    fprintf(logFile.handle, "%s", message.c_str());
    fflush(logFile.handle); // Ensure data is written immediately
  }
}

bool ESPWiFi::shouldLog(LogLevel level) {
  // Treat logging as enabled by default when config isn't loaded yet.
  if (config["log"]["enabled"].isNull()) {
    return true;
  }

  if (!config["log"]["enabled"].as<bool>()) {
    return false;
  }

  std::string configuredLevel = config["log"]["level"].isNull()
                                    ? "info"
                                    : config["log"]["level"].as<std::string>();
  toLowerCase(configuredLevel);

  // Hierarchy: access > debug > info > warn > error
  // access: shows access, debug, info, warning and error
  // debug: shows debug, info, warning and error
  // info: shows info, warning and error
  // warning: shows warning and error
  // error: shows only error

  if (configuredLevel == "access") {
    return true; // Show all levels
  } else if (configuredLevel == "debug") {
    return (level == DEBUG || level == INFO || level == WARNING ||
            level == ERROR);
  } else if (configuredLevel == "info") {
    return (level == INFO || level == WARNING || level == ERROR);
  } else if (configuredLevel == "warning" || configuredLevel == "warn") {
    return (level == WARNING || level == ERROR);
  } else if (configuredLevel == "error") {
    return (level == ERROR);
  }

  // Default: if level is not recognized, allow it (backward compatibility)
  return true;
}

std::string ESPWiFi::formatLog(const char *format, va_list args) {
  char buffer[2048];
  vsnprintf(buffer, sizeof(buffer), format, args);
  return std::string(buffer);
}

std::string logLevelToString(LogLevel level) {
  switch (level) {
  case ACCESS:
    return "[ACCESS]";
  case DEBUG:
    return "[DEBUG]";
  case INFO:
    return " [INFO]";
  case WARNING:
    return " [WARN] ⚠️";
  case ERROR:
    return "[ERROR] 💔";
  default:
    return "[LOG]";
  }
}

void ESPWiFi::log(LogLevel level, const char *format, ...) {
  if (!shouldLog(level)) {
    return;
  }
  if (!serialStarted) {
    startSerial();
  }
  if (!loggingStarted) {
    startLogging();
  }
  va_list args;
  va_start(args, format);
  std::string output = formatLog(format, args);
  va_end(args);
  std::string ts = timestamp();
  std::string levelStr = logLevelToString(level);

  printf("%s%s %s\n", ts.c_str(), levelStr.c_str(), output.c_str());
  fflush(stdout); // Ensure immediate output

  writeLog(ts + levelStr + " " + output + "\n");
}

std::string ESPWiFi::timestamp() {
  // Use esp_timer_get_time() for microseconds since boot
  int64_t time_us = esp_timer_get_time();
  unsigned long milliseconds = time_us / 1000;
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long hours = (seconds % 86400) / 3600;
  unsigned long days = seconds / 86400;
  seconds = seconds % 60;
  milliseconds = milliseconds % 1000;

  // Format all values with consistent padding: 2 digits for days, hours,
  // minutes, seconds; 3 for milliseconds
  char buffer[30];
  snprintf(buffer, sizeof(buffer), "[%02lu:%02lu:%02lu:%02lu:%03lu] ", days,
           hours, minutes, seconds, milliseconds);
  return std::string(buffer);
}

std::string ESPWiFi::timestampForFilename() {
  int64_t time_us = esp_timer_get_time();
  unsigned long milliseconds = time_us / 1000;
  unsigned long seconds = milliseconds / 1000;
  unsigned long days = seconds / 86400;
  unsigned long minutes = (seconds % 86400) / 60;
  seconds = seconds % 60;
  milliseconds = milliseconds % 1000;

  char buffer[50];
  snprintf(buffer, sizeof(buffer), "%lu_%lu_%lu_%lu", days, minutes, seconds,
           milliseconds);
  return std::string(buffer);
}

void ESPWiFi::closeLogFile() {
  if (logFile) {
    logFile.close();
  }
}

void ESPWiFi::openLogFile() {
  // Try SD card first, fallback to LittleFS for logging
  // if (sdCardInitialized && sd) {
  //   logFile = sd->open(logFilePath, "a");
  // } else
  if (lfs && littleFsInitialized) {
    logFile = lfs->open(logFilePath, "a");
    if (!logFile) {
      printf("Warning: Failed to open log file\n");
    }
  }
  // If no filesystem available, just use serial output
}

void ESPWiFi::logConfigHandler() {
  static bool lastEnabled = true;
  static std::string lastLevel = "debug";

  bool currentEnabled = config["log"]["enabled"].as<bool>();
  std::string currentLevel = config["log"]["level"].as<std::string>();

  // Log when enabled state changes
  if (currentEnabled != lastEnabled) {
    log(INFO, "📝 Logging %s", currentEnabled ? "enabled" : "disabled");
    lastEnabled = currentEnabled;
  }

  // Log when level changes
  if (currentLevel != lastLevel) {
    log(INFO, "📝 Log level changed: %s -> %s", lastLevel.c_str(),
        currentLevel.c_str());
    lastLevel = currentLevel;
  }
}

// Commenting out web server log endpoint for now
// void ESPWiFi::srvLog() {
//   // Will implement with ESP-IDF HTTP server later
// }
