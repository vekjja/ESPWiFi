#ifndef ESPWiFi_H
#define ESPWiFi_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <IntervalTimer.h>
#include <LittleFS.h>
#include <WiFiClient.h>

#if CONFIG_IDF_TARGET_ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#elif CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

#ifdef ESPWiFi_CAMERA_ENABLED
#include <esp_camera.h>
#endif

// Forward declaration
class WebSocket;

class ESPWiFi {
 public:
  JsonDocument config;
  String logFile = "/log.txt";
  String configFile = "/config.json";
  AsyncWebServer *webServer = nullptr;

  int connectTimeout = 15000;  // 15 seconds
  void (*connectSubroutine)() = nullptr;

  void startSerial(int baudRate = 115200) {
    if (Serial) {
      return;
    }
    Serial.begin(baudRate);
    Serial.setDebugOutput(true);
    delay(999);  // wait for serial to start
    logf("⛓️  Serial Started:\n\tBaud: %d\n", baudRate);
  }

  void startLittleFS() {
    // Check if LittleFS is already mounted using a static flag
    static bool fsMounted = false;
    if (fsMounted) {
      return;
    }
    if (!LittleFS.begin()) {
      logError("  Failed to mount LittleFS");
      return;
    }
    fsMounted = true;
    log("💾 LittleFS mounted:");
#if CONFIG_IDF_TARGET_ESP8266
    FSInfo fs_info;
    LittleFS.info(fs_info);
    size_t totalBytes = fs_info.totalBytes;
    size_t usedBytes = fs_info.usedBytes;
#elif CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
#endif
    logf("\tUsed: %s\n", bytesToHumanReadable(usedBytes).c_str());
    logf("\tFree: %s\n", bytesToHumanReadable(totalBytes - usedBytes).c_str());
    logf("\tTotal: %s\n", bytesToHumanReadable(totalBytes).c_str());
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
  void startLog(String logFile = "/log.txt");

  void logError(String message);
  void logError(IPAddress ip) { logError(ip.toString()); }
  template <typename T>
  void logError(T value) {
    logError(String(value));
  };

  void log(String message);
  void log(IPAddress ip) { log(ip.toString()); }
  template <typename T>
  void log(T value) {
    log(String(value));
  };

  // Config
  void saveConfig();
  void readConfig();
  void printConfig();
  void defaultConfig();
  void mergeConfig(JsonObject &json);

  // WiFi
  void startAP();
  void startWiFi();
  void startClient();
  int selectBestChannel();

  // mDNS
  void startMDNS();
#if CONFIG_IDF_TARGET_ESP8266
  void updateMDNS();
#endif

  // WebServer
  void srvAll();
  void srvLog();
  void srvRoot();
  void srvInfo();
  void srvFiles();
  void srvConfig();
  void srvRestart();
  void initWebServer();
  void startWebServer();
  void addCORS(AsyncWebServerResponse *response);
  void handleCorsPreflight(AsyncWebServerRequest *request);

  // GPIO
  void startGPIO();
  void startGPIOWebSocket();

  // LED Matrix
  void startLEDMatrix();

  // Spectral Analyzer
  void startSpectralAnalyzer();

  // Camera
#ifdef ESPWiFi_CAMERA_ENABLED
  void startCamera();
  camera_config_t getCamConfig();
  void streamCamera(int frameRate = 10);
  void takeSnapshot(String filePath = "/snapshot.jpg");
#endif

  // RSSI
  void streamRSSI();
  WebSocket *rssiWebSocket = nullptr;

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String &filename);
  String bytesToHumanReadable(size_t bytes);
  void runAtInterval(unsigned int interval, unsigned long &lastIntervalRun,
                     std::function<void()> functionToRun);
};

#endif  // ESPWiFi