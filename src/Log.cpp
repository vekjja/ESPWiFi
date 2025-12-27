#include "ESPWiFi.h"
#include "esp_timer.h"
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
  this->baudRate = baudRate;
  // ESP-IDF: Serial is already initialized by default via UART
  // Just mark it as started
  serialStarted = true;
  vTaskDelay(pdMS_TO_TICKS(300)); // Small delay for serial to stabilize
  printf("%s [INFO] 📺 Serial Started\n", timestamp().c_str());
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

  writeLog("\n\t========= 🌌 ESPWiFi " + version() + " =========\n\n");

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

  // writeLog(ts + levelStr + " " + output + "\n");
}

// Function to check filesystem space and delete log if needed
void ESPWiFi::cleanLogFile() {
  if (maxLogFileSize > -1 && littleFsInitialized) {
    std::string full_path = lfsMountPoint + logFilePath;
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
      if ((size_t)st.st_size > (size_t)maxLogFileSize) {
        log(INFO, "🗑️  Log file deleted");
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

void ESPWiFi::srvLog() {
  if (!webServer) {
    log(ERROR, "Cannot start log API /logs: web server not initialized");
    return;
  }

  // GET /logs - return log file content as HTML
  HTTPRoute("/logs", HTTP_GET, [](httpd_req_t *req) -> esp_err_t {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi->verify(req, true) != ESP_OK) {
      return ESP_OK; // Response already sent (OPTIONS or error)
    }

    // Handler:
    {
      ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

      // Check if LFS is initialized
      if (!espwifi->littleFsInitialized) {
        espwifi->sendJsonResponse(req, 503,
                                  "{\"error\":\"Filesystem not available\"}");
        return ESP_OK;
      }

      // Construct full path to log file
      std::string fullPath = espwifi->lfsMountPoint + espwifi->logFilePath;

      // Check if log file exists
      struct stat fileStat;
      if (stat(fullPath.c_str(), &fileStat) != 0) {
        espwifi->sendJsonResponse(req, 404,
                                  "{\"error\":\"Log file not found\"}");
        return ESP_OK;
      }

      // Open log file
      FILE *file = fopen(fullPath.c_str(), "r");
      if (!file) {
        espwifi->sendJsonResponse(req, 500,
                                  "{\"error\":\"Failed to open log file\"}");
        return ESP_OK;
      }

      // Set content type and CORS headers
      espwifi->addCORS(req);
      httpd_resp_set_type(req, "text/html");

      // Send HTML header with CSS and JavaScript (stream it)
      const char *htmlHeader =
          "<!DOCTYPE html><html><head><meta "
          "charset=\"utf-8\"><style>body{margin:0;padding:10px;background:#"
          "1e1e1e;color:#d4d4d4;font-family:monospace;font-size:12px;}pre{"
          "white-"
          "space:pre;overflow-x:auto;margin:0;}.controls{position:fixed;"
          "top:10px;"
          "right:10px;z-index:1000;background:#2d2d2d;padding:10px;border-"
          "radius:"
          "4px;border:1px solid #444;}.controls "
          "label{display:block;margin:5px "
          "0;color:#d4d4d4;cursor:pointer;}.controls "
          "input[type=\"checkbox\"]{margin-right:8px;cursor:pointer;}</"
          "style><script>var autoScroll=true;var autoRefresh=true;var "
          "refreshInterval;function initControls(){var "
          "scrollCheckbox=document.getElementById('autoScroll');var "
          "refreshCheckbox=document.getElementById('autoRefresh');"
          "autoScroll="
          "localStorage.getItem('autoScroll')!=='false';autoRefresh="
          "localStorage."
          "getItem('autoRefresh')!=='false';if(scrollCheckbox){"
          "scrollCheckbox."
          "checked=autoScroll;scrollCheckbox.addEventListener('change',"
          "function()"
          "{autoScroll=this.checked;localStorage.setItem('autoScroll',"
          "autoScroll)"
          ";if(autoScroll)scrollToBottom();});}if(refreshCheckbox){"
          "refreshCheckbox.checked=autoRefresh;refreshCheckbox."
          "addEventListener('"
          "change',function(){autoRefresh=this.checked;localStorage."
          "setItem('"
          "autoRefresh',autoRefresh);if(autoRefresh){startRefresh();}else{"
          "stopRefresh();}});}if(autoRefresh)startRefresh();}function "
          "scrollToBottom(){if(autoScroll){window.scrollTo(0,document.body."
          "scrollHeight||document.documentElement.scrollHeight);}}function "
          "startRefresh(){if(refreshInterval)clearInterval(refreshInterval)"
          ";"
          "refreshInterval=setInterval(function(){if(autoRefresh)location."
          "reload("
          ");},5000);}function "
          "stopRefresh(){if(refreshInterval){clearInterval(refreshInterval)"
          ";"
          "refreshInterval=null;}}window.addEventListener('load',function()"
          "{"
          "initControls();scrollToBottom();});document.addEventListener('"
          "DOMContentLoaded',function(){setTimeout(scrollToBottom,100);});"
          "setTimeout(scrollToBottom,200);</script></head><body><div "
          "class=\"controls\"><label><input type=\"checkbox\" "
          "id=\"autoScroll\" "
          "checked> Auto Scroll</label><label><input type=\"checkbox\" "
          "id=\"autoRefresh\" checked> Auto Refresh</label></div><pre>";

      esp_err_t ret =
          httpd_resp_send_chunk(req, htmlHeader, strlen(htmlHeader));
      if (ret != ESP_OK) {
        fclose(file);
        return ESP_FAIL;
      }

      // Stream log file content with HTML escaping in small chunks
      // Use heap allocation to avoid stack overflow
      const size_t readChunkSize = 1024;    // Read 1KB at a time
      const size_t escapeBufferSize = 4096; // Escape buffer (4KB max)
      char *readBuffer = (char *)malloc(readChunkSize);
      char *escapeBuffer = (char *)malloc(escapeBufferSize);

      if (!readBuffer || !escapeBuffer) {
        if (readBuffer)
          free(readBuffer);
        if (escapeBuffer)
          free(escapeBuffer);
        fclose(file);
        espwifi->sendJsonResponse(req, 500,
                                  "{\"error\":\"Memory allocation failed\"}");
        return ESP_OK;
      }

      size_t escapePos = 0;
      size_t totalRead = 0;

      while ((ret == ESP_OK) && !feof(file)) {
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield before file I/O
        size_t bytesRead = fread(readBuffer, 1, readChunkSize, file);
        if (bytesRead == 0) {
          break;
        }
        totalRead += bytesRead;

        // Escape HTML special characters and accumulate in buffer
        for (size_t i = 0; i < bytesRead; i++) {
          if (readBuffer[i] == '<') {
            const char *escaped = "&lt;";
            size_t escapedLen = 4;
            if (escapePos + escapedLen >= escapeBufferSize) {
              // Send current buffer
              ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
              if (ret != ESP_OK)
                break;
              escapePos = 0;
              vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O
            }
            memcpy(escapeBuffer + escapePos, escaped, escapedLen);
            escapePos += escapedLen;
          } else if (readBuffer[i] == '>') {
            const char *escaped = "&gt;";
            size_t escapedLen = 4;
            if (escapePos + escapedLen >= escapeBufferSize) {
              // Send current buffer
              ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
              if (ret != ESP_OK)
                break;
              escapePos = 0;
              vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O
            }
            memcpy(escapeBuffer + escapePos, escaped, escapedLen);
            escapePos += escapedLen;
          } else if (readBuffer[i] == '&') {
            const char *escaped = "&amp;";
            size_t escapedLen = 5;
            if (escapePos + escapedLen >= escapeBufferSize) {
              // Send current buffer
              ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
              if (ret != ESP_OK)
                break;
              escapePos = 0;
              vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O
            }
            memcpy(escapeBuffer + escapePos, escaped, escapedLen);
            escapePos += escapedLen;
          } else {
            if (escapePos + 1 >= escapeBufferSize) {
              // Send current buffer
              ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
              if (ret != ESP_OK)
                break;
              escapePos = 0;
              vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O
            }
            escapeBuffer[escapePos++] = readBuffer[i];
          }
        }

        // Send accumulated buffer if it's getting full
        if (escapePos > escapeBufferSize / 2) {
          ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
          if (ret != ESP_OK)
            break;
          escapePos = 0;
          vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O
        }
      }

      // Send any remaining escaped content
      if (ret == ESP_OK && escapePos > 0) {
        ret = httpd_resp_send_chunk(req, escapeBuffer, escapePos);
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O
      }

      // Free allocated buffers
      free(readBuffer);
      free(escapeBuffer);

      fclose(file);

      // Send closing HTML tags
      if (ret == ESP_OK) {
        const char *htmlFooter = "</pre></body></html>";
        ret = httpd_resp_send_chunk(req, htmlFooter, strlen(htmlFooter));
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield after network I/O
      }

      // Finalize chunked transfer
      if (ret == ESP_OK) {
        ret = httpd_resp_send_chunk(req, nullptr, 0); // End chunked transfer
        vTaskDelay(pdMS_TO_TICKS(1));                 // Yield after network I/O
      }

      return (ret == ESP_OK) ? ESP_OK : ESP_FAIL;
    }
  });
}
