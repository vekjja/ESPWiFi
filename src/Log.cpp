#ifndef ESPWiFi_LOG
#define ESPWiFi_LOG

#include "ESPWiFi.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <cstring>
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
  // ESP-IDF: Serial is already initialized by default via UART
  // Just mark it as started
  this->baudRate = baudRate;
  serialStarted = true;
  vTaskDelay(pdMS_TO_TICKS(300)); // Small delay for serial to stabilize
  printf("%s  [INFO] 📺 Serial Started\n", timestamp().c_str());
  printf("%s  [INFO]\tBaud: %d\n", timestamp().c_str(), getSerialBaudRate());
}

void ESPWiFi::startLogging(std::string filePath) {
  if (loggingStarted) {
    return;
  }
  loggingStarted = true;

  // Ensure filesystems are ready. SD is config-gated in initSDCard().
  initLittleFS();
  initSDCard();

  if (logFileMutex == nullptr) {
    logFileMutex = xSemaphoreCreateMutex();
  }

  // Config-driven logging:
  // - log.enabled
  // - log.level
  // - log.useSD
  // - log.file (string path, e.g. "/log")
  std::string cfgFile = config["log"]["file"].as<std::string>();

  // Pick file path (config overrides default param).
  if (!cfgFile.empty()) {
    filePath = cfgFile;
  }
  if (filePath.empty()) {
    filePath = "/log";
  }
  if (filePath.front() != '/') {
    filePath.insert(filePath.begin(), '/');
  }
  this->logFilePath = filePath;

  cleanLogFile();

  writeLog("\n========= 🌈 ESPWiFi " + version() + " =========\n\n");

  if (serialStarted) {
    log(INFO, "📺 Serial Output Enabled");
    log(DEBUG, "📺\tBaud: %d", getSerialBaudRate());
  }

  printFilesystemInfo();
}

FILE *ESPWiFi::openLogFile(bool useSD) {
  std::string full_path =
      useSD ? sdMountPoint + logFilePath : lfsMountPoint + logFilePath;
  return fopen(full_path.c_str(), "a");
}

bool ESPWiFi::getLogFilesystem(bool &useSD, bool &useLFS) {
  checkSDCardPresent();
  // LittleFS
  bool preferSD = config["log"]["useSD"].as<bool>();
  useSD = preferSD && (sdCard != nullptr);
  // LFS is always available as a fallback (even if SD is preferred)
  useLFS = (lfs != nullptr);
  return useSD || useLFS; // Returns true if a filesystem is available
}

void ESPWiFi::writeLog(std::string message) {
  bool useSD, useLFS;
  if (!getLogFilesystem(useSD, useLFS)) {
    return;
  }

  // Best-effort mutex: wait briefly to reduce dropped *file* log lines, but
  // keep the wait bounded so logging can't noticeably stall request handling.
  // (Serial output still prints even if we return early.)
  if (logFileMutex != nullptr) {
    if (xSemaphoreTake(logFileMutex, pdMS_TO_TICKS(18)) != pdTRUE) {
      return;
    }
  }

  // Try SD first if preferred, fallback to LFS on failure
  bool success = false;

  if (useSD) {
    std::string full_path = sdMountPoint + logFilePath;
    FILE *f = fopen(full_path.c_str(), "a");
    if (f) {
      size_t len = message.length();
      size_t written = fwrite(message.c_str(), 1, len, f);
      if (written == len && fflush(f) == 0) {
        success = true;
      } else {
        // Write or flush failed - card may have been removed
        // Directly deinit to avoid recursion (handleSDCardError calls log())
        if (sdCard != nullptr) {
          deinitSDCard();
        }
      }
      fclose(f);
    } else {
      // Open failed on SD card - may have been removed
      // Directly deinit to avoid recursion (handleSDCardError calls log())
      success = false;
      if (sdCard != nullptr) {
        deinitSDCard();
      }
    }
  }

  // If SD failed or wasn't preferred, try LFS
  if (!success && useLFS) {
    std::string full_path = lfsMountPoint + logFilePath;
    FILE *f = fopen(full_path.c_str(), "a");
    if (f) {
      size_t len = message.length();
      size_t written = fwrite(message.c_str(), 1, len, f);
      if (written == len && fflush(f) == 0) {
        success = true;
        // Note: Don't log the switch here to avoid any potential recursion
        // The switch will be logged on the next log message via normal logging
      }
      fclose(f);
    }
  }

  if (logFileMutex != nullptr) {
    xSemaphoreGive(logFileMutex);
  }
}

void ESPWiFi::logImpl(LogLevel level, const std::string &message) {
  if (!shouldLog(level)) {
    return;
  }
  if (!serialStarted) {
    startSerial();
  }

  std::string ts = timestamp();
  std::string levelStr = logLevelToString(level);

  // Direct printf with format - avoids string allocations
  printf("%s %s %s\n", ts.c_str(), levelStr.c_str(), message.c_str());

  std::string logLine = ts + levelStr + " " + message + "\n";
  writeLog(logLine);
}

void ESPWiFi::log(LogLevel level, const char *format, ...) {
  va_list args;
  va_start(args, format);

  // Format the message
  char buffer[1536];
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  logImpl(level, std::string(buffer));
}

void ESPWiFi::log(LogLevel level, const char *format, const std::string &arg) {
  char buffer[1536];
  snprintf(buffer, sizeof(buffer), format, arg.c_str());
  logImpl(level, std::string(buffer));
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::cleanLogFile() {
  bool useSD, useLFS;
  if (!getLogFilesystem(useSD, useLFS) || maxLogFileSize < 0) {
    return;
  }

  const std::string &base = useSD ? sdMountPoint : lfsMountPoint;
  std::string full_path = base + logFilePath;

  if (logFileMutex != nullptr) {
    (void)xSemaphoreTake(logFileMutex, pdMS_TO_TICKS(5));
  }
  struct stat st;
  if (stat(full_path.c_str(), &st) == 0) {
    if ((size_t)st.st_size > (size_t)maxLogFileSize) {
      // Use printf instead of log() to avoid recursion during cleanup
      printf("%s  [INFO] 🗑️  Log file deleted\n", timestamp().c_str());
      bool deleted = ::remove(full_path.c_str()) == 0;
      if (!deleted) {
        printf("%s [ERROR] 💔 Failed to delete log file\n",
               timestamp().c_str());
      }
    }
  }
  if (logFileMutex != nullptr) {
    xSemaphoreGive(logFileMutex);
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

  // Hierarchy: verbose > access > debug > info > warn > error
  // verbose: shows verbose, access, debug, info, warning and error
  // access: shows access, debug, info, warning and error
  // debug: shows debug, info, warning and error
  // info: shows info, warning and error
  // warning: shows warning and error
  // error: shows only error

  if (configuredLevel == "verbose") {
    return true; // Show all levels
  } else if (configuredLevel == "access") {
    return (level == ACCESS || level == DEBUG || level == INFO ||
            level == WARNING || level == ERROR);
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

void ESPWiFi::logConfigHandler() {
  // Default values (used for comparison on first run and as fallback)
  static bool lastEnabled = true;
  static bool lastuseSD = true;
  static std::string lastLevel = "debug";
  static std::string lastFile = "/log";

  const bool currentEnabled = config["log"]["enabled"].isNull()
                                  ? true
                                  : config["log"]["enabled"].as<bool>();
  std::string currentLevel = config["log"]["level"].isNull()
                                 ? "debug"
                                 : config["log"]["level"].as<std::string>();
  const bool currentuseSD = config["log"]["useSD"].isNull()
                                ? true
                                : config["log"]["useSD"].as<bool>();

  std::string currentFile;
  if (!config["log"]["file"].isNull()) {
    currentFile = config["log"]["file"].as<std::string>();
  } else if (!config["log"]["filePath"].isNull()) {
    currentFile = config["log"]["filePath"].as<std::string>();
  } else {
    currentFile = "/log";
  }
  if (currentFile.empty()) {
    currentFile = "/log";
  }
  if (currentFile.front() != '/') {
    currentFile.insert(currentFile.begin(), '/');
  }

  // Check for changes and apply them
  bool needLogEnabledMsg = (currentEnabled != lastEnabled);
  bool needLevelMsg = (currentLevel != lastLevel);
  bool needFileMsg = (currentFile != lastFile);
  bool needPreferMsg = (currentuseSD != lastuseSD);

  if (needFileMsg) {
    // Best-effort: synchronize with file writes.
    if (logFileMutex != nullptr) {
      (void)xSemaphoreTake(logFileMutex, pdMS_TO_TICKS(5));
    }
    logFilePath = currentFile;
    if (logFileMutex != nullptr) {
      xSemaphoreGive(logFileMutex);
    }
  }

  if (needLogEnabledMsg) {
    log(INFO, "📝 Logging %s", currentEnabled ? "enabled" : "disabled");
    lastEnabled = currentEnabled;
  }
  if (needLevelMsg) {
    log(INFO, "📝 Log level: %s -> %s", lastLevel.c_str(),
        currentLevel.c_str());
    lastLevel = currentLevel;
  }
  if (needPreferMsg) {
    log(INFO, "📝 Log useSD: %s -> %s", lastuseSD ? "true" : "false",
        currentuseSD ? "true" : "false");
    lastuseSD = currentuseSD;
  }
  if (needFileMsg) {
    log(INFO, "📝 Log file: %s -> %s", lastFile.c_str(), currentFile.c_str());
    lastFile = currentFile;
  }
}

#endif // ESPWiFi_LOG