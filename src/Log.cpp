#include "ESPWiFi.h"
#include "esp_timer.h"
#include <stdarg.h>
#include <sys/time.h>

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

void ESPWiFi::startSerial(int baudRate) {
  if (serialStarted) {
    return;
  }
  this->baudRate = baudRate;
  // ESP-IDF: Serial is already initialized by default via UART
  // Just mark it as started
  serialStarted = true;
  vTaskDelay(pdMS_TO_TICKS(500)); // Small delay for serial to stabilize
  printf("%s [INFO] 📺  Serial Started\n", timestamp().c_str());
  printf("%s [INFO]\tBaud: %d\n", timestamp().c_str(), baudRate);
}

void ESPWiFi::startLogging(std::string filePath) {
  if (loggingStarted) {
    return;
  }
  loggingStarted = true;

  if (!serialStarted) {
    startSerial();
  }

  initLittleFS();
  // initSDCard();  // Commented out for now
  this->logFilePath = filePath;

  cleanLogFile();

  log(INFO, "\n\n\t========= 🌌 ESPWiFi %s =========\n", version().c_str());

  if (serialStarted) {
    log(INFO, "📺 Serial Output Enabled");
    log(DEBUG, "\tBaud: %d", baudRate);
  }

  printFilesystemInfo();
}

void ESPWiFi::writeLog(std::string message) {
  if (!littleFsInitialized) {
    return;
  }

  std::string full_path = lfsMountPoint + logFilePath;
  FILE *f = fopen(full_path.c_str(), "a");
  if (f) {
    size_t len = message.length();
    size_t written = fwrite(message.c_str(), 1, len, f);
    if (written != len) {
      printf("Warning: Failed to write complete message to log file\n");
    }
    fflush(f);
    fclose(f);
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

// Function to check filesystem space and delete log if needed
void ESPWiFi::cleanLogFile() {
  if (maxLogFileSize > -1 && littleFsInitialized) {
    std::string full_path = lfsMountPoint + logFilePath;
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if ((size_t)st.st_size > (size_t)maxLogFileSize) {
        log(INFO, "🗑️ Log file deleted");
        bool deleted = ::remove(full_path.c_str()) == 0;
        if (!deleted) {
          log(ERROR, "Failed to delete log file");
        }
      }
    }
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

  // Format all values with consistent padding: [00:00:00:00:000]
  // 2 digits for days, hours, minutes, seconds; 3 for milliseconds
  // but if the value is 00 then don't show it
  char buffer[30];
  if (days > 0) {
    snprintf(buffer, sizeof(buffer), "[%02lu:%02lu:%02lu:%02lu:%03lu] ", days,
             hours, minutes, seconds, milliseconds);
  } else if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "[%02lu:%02lu:%02lu:%03lu] ", hours,
             minutes, seconds, milliseconds);
  } else {
    snprintf(buffer, sizeof(buffer), "[%02lu:%02lu:%03lu] ", minutes, seconds,
             milliseconds);
  }
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
