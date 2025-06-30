#ifndef ESPWiFi_H
#define ESPWiFi_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <IntervalTimer.h>
#include <LittleFS.h>
#include <WiFiClient.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

// Forward declaration
class WebSocket;

class ESPWiFi {
public:
  JsonDocument config;
  String logFile = "/log.txt";
  bool loggingEnabled = false;
  String configFile = "/config.json";
  AsyncWebServer *webServer = nullptr;

  int connectTimeout = 15000; // 15 seconds
  void (*connectSubroutine)() = nullptr;

  void startSerial(int baudRate = 115200) {
    if (Serial) {
      return;
    }
    Serial.begin(baudRate);
    Serial.setDebugOutput(true);
    delay(999); // wait for serial to start
    logf("⛓️  Serial Started:\n\tBaud: %d\n", baudRate);
  }

  void startLittleFS() {
    // Check if LittleFS is already mounted using a static flag
    static bool fsMounted = false;
    if (fsMounted) {
      return;
    }
    if (!LittleFS.begin()) {
      log("❌  Failed to mount LittleFS");
      return;
    }
    fsMounted = true;
    log("💾 LittleFS mounted:");
#ifdef ESP8266
    FSInfo fs_info;
    LittleFS.info(fs_info);
    size_t totalBytes = fs_info.totalBytes;
    size_t usedBytes = fs_info.usedBytes;
#elif defined(ESP32)
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
#endif
    logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
    logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
    logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
  }

  void startLog(String logFile = "/log.txt") {
    startSerial();
    startLittleFS();
    loggingEnabled = true;
    this->logFile = logFile;
    log("🔍 Logging started:");
    logf("\tFile: %s\n", logFile.c_str());
  }

  // Power Management
  void stopSleep();
  void enablePowerSaving();
  void applyPowerSettings();
  void optimizeForFastResponse();

  // Log
  void closeLog();
  void checkAndCleanupLogFile();
  void writeLog(String message);
  void logf(const char *format, ...);
  void log(String message);
  void log(IPAddress ip) { log(ip.toString()); }
  template <typename T> void log(T value) {
    String valueString = String(value);
    log(valueString);
  }

  // Config
  void saveConfig();
  void readConfig();
  void defaultConfig();

  // WiFi
  void startAP();
  void startWiFi();
  void startClient();
  int selectBestChannel();

  // mDNS
  void startMDNS();
#ifdef ESP8266
  void mDSNUpdate();
#endif

  // Web Server
  void initWebServer();
  void startWebServer();
  void addCORS(AsyncWebServerResponse *response);

  // GPIO
  void startGPIO();
  void startGPIOWebSocket();

  // Camera
#ifdef ESPWiFi_CAMERA_ENABLED
  void startCamera();
  void streamCamera(int frameRate = 10);
  void takeSnapshot(String filePath = "/snapshot.jpg");
#endif

  // RSSI
  void streamRssi();
  WebSocket *rssiWebSocket = nullptr;

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String &filename);
  String bytesToHumanReadable(size_t bytes);
  void runAtInterval(unsigned int interval, unsigned long &lastIntervalRun,
                     std::function<void()> functionToRun);

private:
  void handleCorsPreflight(AsyncWebServerRequest *request);
};

#endif // ESPWiFi