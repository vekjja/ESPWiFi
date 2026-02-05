#ifndef ESPWiFi_LOG
#define ESPWiFi_LOG

#include "ESPWiFi.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <stdarg.h>
#include <sys/time.h>

// ---- ESP-IDF log capture (optional)
//
// ESP-IDF logging (ESP_LOGx) ultimately prints via a vprintf-like function
// that can be replaced with esp_log_set_vprintf(). By hooking this once, we can
// capture ALL ESP-IDF logs (like httpd_txrx) without subscribing to events.
//
// Important: Our normal ESPWiFi::logImpl() uses printf(), which would recurse
// back into this hook. So the hook writes to the log FILE only via writeLog().
static vprintf_like_t s_prevEspVprintf = nullptr;
static ESPWiFi *s_espwifiForEspLogs = nullptr;
static bool s_espLogHookInstalled = false;
static std::string s_lastIdfTag;

static const char *skipAnsiAndWhitespace(const char *p) {
  if (p == nullptr) {
    return nullptr;
  }
  // Skip ANSI color sequences (ESC [ ... m)
  while (*p == '\033') {
    if (*(p + 1) == '[') {
      p += 2;
      while (*p != '\0' && *p != 'm') {
        ++p;
      }
      if (*p == 'm') {
        ++p;
        continue;
      }
    }
    break;
  }
  // Skip whitespace
  while (*p == ' ' || *p == '\t') {
    ++p;
  }
  return p;
}

struct ParsedIdfLine {
  bool isIdf = false;
  LogLevel level = INFO;
  std::string tag;
  std::string body; // may be empty for tag-only lines like "I (123) wifi:"
};

static inline bool tagEquals(const char *tag, size_t tagLen, const char *lit) {
  const size_t litLen = std::strlen(lit);
  if (tagLen != litLen) {
    return false;
  }
  return std::memcmp(tag, lit, tagLen) == 0;
}

static const char *espwifiIconForIdfTagView(const char *tag, size_t tagLen) {
  if (tag == nullptr || tagLen == 0) {
    return "";
  }
  if (tagEquals(tag, tagLen, "wifi") || tagEquals(tag, tagLen, "net80211") ||
      tagEquals(tag, tagLen, "wifi_init") ||
      tagEquals(tag, tagLen, "esp_netif_handlers") ||
      tagEquals(tag, tagLen, "phy_init") || tagEquals(tag, tagLen, "pp")) {
    return "📶";
  }
  if (tagEquals(tag, tagLen, "httpd") || tagEquals(tag, tagLen, "httpd_txrx") ||
      tagEquals(tag, tagLen, "httpd_uri")) {
    return "🗄️";
  }
  if (tagEquals(tag, tagLen, "mdns") || tagEquals(tag, tagLen, "mdns_mem")) {
    return "🏷️";
  }
  if (tagEquals(tag, tagLen, "websocket_client")) {
    return "🌐";
  }
  if (tagEquals(tag, tagLen, "cam_hal") || tagEquals(tag, tagLen, "camera") ||
      tagEquals(tag, tagLen, "esp_camera") ||
      tagEquals(tag, tagLen, "s3 ll_cam") ||
      tagEquals(tag, tagLen, "sccb-ng") || tagEquals(tag, tagLen, "ov3660")) {
    return "📷";
  }
  if (tagEquals(tag, tagLen, "BTDM_INIT") || tagEquals(tag, tagLen, "BT") ||
      tagEquals(tag, tagLen, "BLE_INIT") || tagEquals(tag, tagLen, "NimBLE") ||
      tagEquals(tag, tagLen, "nimble")) {
    return "🔵";
  }
  if (tagEquals(tag, tagLen, "TFT") || tagEquals(tag, tagLen, "ili9341")) {
    return "🖥️";
  }
  if (tagEquals(tag, tagLen, "esp-x509-crt-bundle")) {
    return "🔐";
  }
  return "";
}

int ESPWiFi::idfLogVprintfHook(const char *format, va_list args) {

  // Static member function for ESP-IDF's vprintf hook
  // Captures and processes ESP-IDF log messages
  ESPWiFi *espwifi = s_espwifiForEspLogs;
  if (espwifi == nullptr) {
    return 0;
  }

  // This runs in system tasks with small stacks, so keep it minimal
  char line[256];
  {
    va_list argsCopy;
    va_copy(argsCopy, args);
    (void)vsnprintf(line, sizeof(line), format, argsCopy);
    va_end(argsCopy);

    // Format and log the IDF message
    LogLevel lvl = DEBUG;
    std::string formatted = espwifi->formatIDFtoESPWiFi(line, &lvl);
    if (!formatted.empty()) {
      espwifi->logImpl(lvl, formatted);
    }
  }

  return 0;
}

static void installEspIdfLogCapture(ESPWiFi *espwifi) {
  s_espwifiForEspLogs = espwifi;
  if (s_espLogHookInstalled) {
    return;
  }
  s_prevEspVprintf = esp_log_set_vprintf(&ESPWiFi::idfLogVprintfHook);
  s_espLogHookInstalled = true;
}

std::string ESPWiFi::logLevelToString(LogLevel level) {
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
    return "[ERROR] ❗️";
  default:
    return "[LOG]";
  }
}

void ESPWiFi::startLogging() {
  if (loggingStarted) {
    return;
  }
  loggingStarted = true;

  if (logFileMutex == nullptr) {
    logFileMutex = xSemaphoreCreateMutex();
  }

  if (deferredLogMutex == nullptr) {
    deferredLogMutex = xSemaphoreCreateMutex();
  }

  std::string cfgLogFile = config["log"]["file"].as<std::string>();
  std::string filePath = logFilePath;

  // Pick file path (config overrides default param).
  if (!cfgLogFile.empty()) {
    filePath = cfgLogFile;
  }
  if (filePath.empty()) {
    filePath = "/espwifi.log";
  }
  if (filePath.front() != '/') {
    filePath.insert(filePath.begin(), '/');
  }
  this->logFilePath = filePath;

  cleanLogFile();

  writeLog("\n========= 🌈 ESPWiFi " + version() + " =========\n\n");

  // ESP32 IDF Automatically enables serial output at 115200 baud
  log(INFO, "📺 Serial Output Enabled");
  log(INFO, "📺\tBaud: 115200");

  printFilesystemInfo();

  // Capture ESP-IDF logs (ESP_LOGx) into the same espwifi log file, so
  // warnings like `httpd_txrx` show up without having to catch ESP events.
  installEspIdfLogCapture(this);
}

bool ESPWiFi::getLogFilesystem(bool &useSD, bool &useLFS) {
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
        // The switch will be logged on the next log message via normal
        // logging
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

  if (logFileMutex != nullptr) {
    (void)xSemaphoreTake(logFileMutex, pdMS_TO_TICKS(5));
  }

  struct stat st;
  bool deletedAny = false;

  // Check and delete from SD card if available
  if (useSD) {
    std::string sd_path = sdMountPoint + logFilePath;
    if (stat(sd_path.c_str(), &st) == 0) {
      if ((size_t)st.st_size > (size_t)maxLogFileSize) {
        bool deleted = ::remove(sd_path.c_str()) == 0;
        if (deleted) {
          deletedAny = true;
        } else {
          printf("%s [ERROR] ❗️ Failed to delete log file from SD\n",
                 timestamp().c_str());
        }
      }
    }
  }

  // Check and delete from LFS if available
  if (useLFS) {
    std::string lfs_path = lfsMountPoint + logFilePath;
    if (stat(lfs_path.c_str(), &st) == 0) {
      if ((size_t)st.st_size > (size_t)maxLogFileSize) {
        bool deleted = ::remove(lfs_path.c_str()) == 0;
        if (deleted) {
          deletedAny = true;
        } else {
          printf("%s [ERROR] ❗️ Failed to delete log file from LFS\n",
                 timestamp().c_str());
        }
      }
    }
  }

  if (deletedAny) {
    // Use printf instead of log() to avoid recursion during cleanup
    printf("%s  [INFO] 🗑️  Log file deleted\n", timestamp().c_str());
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
  static std::string lastFile = "/espwifi.log";

  // Config is now replaced atomically in handleConfigUpdate() before this is
  // called, so we can safely read it here. Extract values for comparison.
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
    currentFile = "/espwifi.log";
  }
  if (currentFile.empty()) {
    currentFile = "/espwifi.log";
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

// Helper function to format ESP-IDF log lines into ESPWiFi format
// Returns empty string if the line should be skipped
std::string ESPWiFi::formatIDFtoESPWiFi(const std::string &line,
                                        LogLevel *outLevel) {
  const size_t len = line.length();
  if (len == 0) {
    return "";
  }

  // Check if line is empty or only whitespace/newlines
  bool hasContent = false;
  for (const char *c = line.c_str(); *c != '\0'; c++) {
    if (*c != ' ' && *c != '\t' && *c != '\n' && *c != '\r') {
      hasContent = true;
      break;
    }
  }
  if (!hasContent) {
    return ""; // Skip lines with only whitespace
  }

  // Extract ESP-IDF tag for icon mapping:
  // Format: [E/W/I/D/V] ' ' '(' ... ')' ' ' <tag> ':' ...
  const char *icon = "";
  const char *prefix = skipAnsiAndWhitespace(line.c_str());

  // Infer log level from the formatted line prefix
  LogLevel lvl = DEBUG;
  if (prefix != nullptr) {
    switch (*prefix) {
    case 'E':
      lvl = ERROR;
      break;
    case 'W':
      lvl = WARNING;
      break;
    case 'I':
      lvl = DEBUG;
      break;
    case 'D':
      lvl = DEBUG;
      break;
    case 'V':
      lvl = VERBOSE;
      break;
    default:
      break;
    }
  }

  // Check if we should log this level
  if (!shouldLog(lvl)) {
    return "";
  }

  // Extract icon from tag if present and find the actual message content
  const char *messageStart = nullptr;
  if (prefix != nullptr) {
    const char *closeParen = std::strstr(prefix, ") ");
    if (closeParen != nullptr) {
      const char *tagStart = closeParen + 2;
      const char *colon = std::strchr(tagStart, ':');
      if (colon != nullptr && colon > tagStart) {
        icon = espwifiIconForIdfTagView(tagStart, (size_t)(colon - tagStart));
        // Message content starts after the colon and space
        messageStart = colon + 1;
        // Skip whitespace after colon
        while (*messageStart == ' ' || *messageStart == '\t') {
          messageStart++;
        }
      }
    }
  }

  // Skip lines that have no actual message content after the tag
  // (e.g., "I (1726) wifi:\n" with nothing after the colon)
  if (messageStart != nullptr) {
    // Check if there's any non-whitespace content
    bool hasContent = false;
    for (const char *c = messageStart; *c != '\0' && *c != '\n' && *c != '\r';
         c++) {
      if (*c != ' ' && *c != '\t') {
        hasContent = true;
        break;
      }
    }
    if (!hasContent) {
      return ""; // Skip empty lines
    }
  } else {
    icon = "📶"; // Add wifi icon for untagged logs
  }

  // Build the formatted log line
  // icon + message content
  std::string out;
  out.reserve(64 + len);
  if (icon[0] != '\0') {
    out.append(icon);
    out.push_back(' ');
  }

  // Append the line, but strip trailing newlines/whitespace
  // (logImpl will add its own newline)
  size_t endPos = len;
  while (endPos > 0 && (line[endPos - 1] == '\n' || line[endPos - 1] == '\r')) {
    endPos--;
  }
  out.append(line, 0, endPos);

  // Set output level for caller
  if (outLevel != nullptr) {
    *outLevel = lvl;
  }
  return out;
}

bool ESPWiFi::logIDF(std::string message) {
  // Thread-safe: add the raw ESP-IDF log line to the deferred queue
  // This is called from ESP-IDF system tasks with small stacks,
  // so we keep this minimal and defer processing to the main thread.

  if (deferredLogMutex != nullptr) {
    // Use a very short timeout to avoid blocking system tasks
    if (xSemaphoreTake(deferredLogMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
      return false; // Failed to acquire mutex, drop this log
    }
  }

  // Add to deferred queue (using move semantics to avoid copy)
  deferredLogMessages.push_back(std::move(message));

  if (deferredLogMutex != nullptr) {
    xSemaphoreGive(deferredLogMutex);
  }

  return true;
}

void ESPWiFi::flushDeferredLog() {
  // Process all deferred ESP-IDF log messages in the main thread
  // This avoids stack overflow issues in system tasks

  if (deferredLogMessages.empty()) {
    return;
  }

  // Grab a local copy of the messages to process
  std::vector<std::string> messagesToProcess;
  if (deferredLogMutex != nullptr) {
    if (xSemaphoreTake(deferredLogMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
      return; // Can't acquire mutex, try again later
    }
  }

  messagesToProcess.swap(deferredLogMessages);
  deferredLogMessages.clear(); // Ensure the queue is empty

  if (deferredLogMutex != nullptr) {
    xSemaphoreGive(deferredLogMutex);
  }

  // Now process each message (outside the mutex lock)
  for (const std::string &line : messagesToProcess) {
    LogLevel lvl = INFO;
    std::string formatted = formatIDFtoESPWiFi(line, &lvl);

    // Skip if the line should be filtered out (empty return)
    if (formatted.empty()) {
      continue;
    }

    // Log the formatted line
    log(lvl, formatted);
  }
}

#endif // ESPWiFi_LOG