#ifndef ESPWIFI_LOG
#define ESPWIFI_LOG

#include "ESPWiFi.h"
#include <stdarg.h>

// Global file handle for logging
static File logFileHandle;

void ESPWiFi::writeLog(String message) {
  if (logFileHandle) {
    logFileHandle.print(message);
    logFileHandle.flush(); // Ensure data is written immediately
  } else {
    startLittleFS();
    logFileHandle = LittleFS.open(logFile, "a");
    writeLog(message);
  }
}

void ESPWiFi::log(String message) {
  Serial.println(message);
  writeLog(message + "\n");
}

void ESPWiFi::logf(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  Serial.print(buffer);
  writeLog(buffer);
}

// Add a method to close the log file (call this in setup or when needed)
void ESPWiFi::closeLog() {
  if (logFileHandle) {
    logFileHandle.close();
  }
}

#endif // ESPWIFI_LOG
