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
  Serial.println(timestamp() + "📺  Serial Started:");
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

  writeLog("\n\n\t\t\t🌌 FirmaMint " + version + "\n\n");

  if (Serial) {
    log(INFO, "📺 Serial Output Enabled");
    log(DEBUG, "\tBaud: %d", baudRate);
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

      if (!deleted) {
        log(ERROR, "Failed to delete log file");
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

bool ESPWiFi::shouldLog(LogLevel level) {
  // Treat logging as enabled by default when config isn't loaded yet.
  if (config["log"]["enabled"].isNull()) {
    return true;
  }

  if (!config["log"]["enabled"].as<bool>()) {
    return false;
  }

  String configuredLevel = config["log"]["level"].isNull()
                               ? "info"
                               : config["log"]["level"].as<String>();
  configuredLevel.toLowerCase();

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

String ESPWiFi::formatLog(const char *format, va_list args) {
  char buffer[2048];
  vsnprintf(buffer, sizeof(buffer), format, args);
  return String(buffer);
}

String logLevelToString(LogLevel level) {
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
    // return "📝 [LOG]";
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
  String output = formatLog(format, args);
  va_end(args);
  String ts = timestamp();
  String levelStr = logLevelToString(level);
  Serial.println(ts + levelStr + " " + output);
  Serial.flush(); // Ensure immediate output
  writeLog(ts + levelStr + " " + output + "\n");
}

String ESPWiFi::timestamp() {
  unsigned long milliseconds = millis();
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = (seconds % 3600) / 60;
  unsigned long hours = (seconds % 86400) / 3600;
  unsigned long days = seconds / 86400;
  seconds = seconds % 60;
  milliseconds = milliseconds % 1000;

  // Format all values with consistent padding: 2 digits for days, hours,
  // minutes, seconds; 3 for milliseconds
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "[%02lu:%02lu:%02lu:%02lu:%03lu] ", days,
           hours, minutes, seconds, milliseconds);
  return String(buffer);
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
    log(ERROR, "No file system available for logging");
    return;
  }

  if (!logFile) {
    log(ERROR, "Failed to open log file");
  }
}

void ESPWiFi::logConfigHandler() {
  static bool lastEnabled = true;
  static String lastLevel = "info";

  bool currentEnabled = config["log"]["enabled"].as<bool>();
  String currentLevel = config["log"]["level"].as<String>();

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

void ESPWiFi::srvLog() {
  initWebServer();

  // GET /log - return log file content
  webServer->on("/logs", HTTP_GET, [this](AsyncWebServerRequest *request) {
    if (request->method() == HTTP_OPTIONS) {
      handleCorsPreflight(request);
      return;
    }

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

    // Read log file and wrap in HTML with CSS to prevent word wrapping
    File file = filesystem->open(logFilePath, "r");
    if (!file) {
      sendJsonResponse(request, 500, "{\"error\":\"Failed to open log file\"}");
      return;
    }

    String htmlContent =
        "<!DOCTYPE html><html><head><meta "
        "charset=\"utf-8\"><style>body{margin:0;padding:10px;background:#"
        "1e1e1e;color:#d4d4d4;font-family:monospace;font-size:12px;}pre{white-"
        "space:pre;overflow-x:auto;margin:0;}.controls{position:fixed;top:10px;"
        "right:10px;z-index:1000;background:#2d2d2d;padding:10px;border-radius:"
        "4px;border:1px solid #444;}.controls label{display:block;margin:5px "
        "0;color:#d4d4d4;cursor:pointer;}.controls "
        "input[type=\"checkbox\"]{margin-right:8px;cursor:pointer;}</"
        "style><script>var autoScroll=true;var autoRefresh=true;var "
        "refreshInterval;function initControls(){var "
        "scrollCheckbox=document.getElementById('autoScroll');var "
        "refreshCheckbox=document.getElementById('autoRefresh');autoScroll="
        "localStorage.getItem('autoScroll')!=='false';autoRefresh=localStorage."
        "getItem('autoRefresh')!=='false';if(scrollCheckbox){scrollCheckbox."
        "checked=autoScroll;scrollCheckbox.addEventListener('change',function()"
        "{autoScroll=this.checked;localStorage.setItem('autoScroll',autoScroll)"
        ";if(autoScroll)scrollToBottom();});}if(refreshCheckbox){"
        "refreshCheckbox.checked=autoRefresh;refreshCheckbox.addEventListener('"
        "change',function(){autoRefresh=this.checked;localStorage.setItem('"
        "autoRefresh',autoRefresh);if(autoRefresh){startRefresh();}else{"
        "stopRefresh();}});}if(autoRefresh)startRefresh();}function "
        "scrollToBottom(){if(autoScroll){window.scrollTo(0,document.body."
        "scrollHeight||document.documentElement.scrollHeight);}}function "
        "startRefresh(){if(refreshInterval)clearInterval(refreshInterval);"
        "refreshInterval=setInterval(function(){if(autoRefresh)location.reload("
        ");},5000);}function "
        "stopRefresh(){if(refreshInterval){clearInterval(refreshInterval);"
        "refreshInterval=null;}}window.addEventListener('load',function(){"
        "initControls();scrollToBottom();});document.addEventListener('"
        "DOMContentLoaded',function(){setTimeout(scrollToBottom,100);});"
        "setTimeout(scrollToBottom,200);</script></head><body><div "
        "class=\"controls\"><label><input type=\"checkbox\" id=\"autoScroll\" "
        "checked> Auto Scroll</label><label><input type=\"checkbox\" "
        "id=\"autoRefresh\" checked> Auto Refresh</label></div><pre>";

    // Read file in chunks and append to HTML
    const size_t chunkSize = 1024;
    char buffer[chunkSize];
    while (file.available()) {
      size_t bytesRead = file.readBytes(buffer, chunkSize);
      // Escape HTML special characters
      for (size_t i = 0; i < bytesRead; i++) {
        if (buffer[i] == '<') {
          htmlContent += "&lt;";
        } else if (buffer[i] == '>') {
          htmlContent += "&gt;";
        } else if (buffer[i] == '&') {
          htmlContent += "&amp;";
        } else {
          htmlContent += buffer[i];
        }
      }
    }
    file.close();

    htmlContent += "</pre></body></html>";

    AsyncWebServerResponse *response =
        request->beginResponse(200, "text/html", htmlContent);
    addCORS(response);
    request->send(response);
  });
}
