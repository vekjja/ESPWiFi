#ifndef ESPWiFi_H
#define ESPWiFi_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <IntervalTimer.h>
#include <LittleFS.h>
#include <SD.h>
#include <Update.h>
#include <WiFiClient.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

#ifdef ESPWiFi_CAMERA_INSTALLED
#include <esp_camera.h>
#endif

// Forward declaration
class WebSocket;

class ESPWiFi {
private:
  String version = "v0.1.0";

public:
  JsonDocument config;
  int connectTimeout = 15000;
  String configFile = "/config.json";
  AsyncWebServer *webServer = nullptr;
  void (*connectSubroutine)() = nullptr;

  // Device
  void start();
  void runSystem();

  // File System
  FS *lfs = nullptr; // LittleFS handle
  FS *sd = nullptr;  // SD card handle
  void initSDCard();
  void initLittleFS();
  bool sdCardInitialized = false;
  bool littleFsInitialized = false;
  bool fileExists(FS *fs, const String &filePath);
  bool dirExists(FS *fs, const String &dirPath);
  bool mkDir(FS *fs, const String &dirPath);
  bool deleteDirectoryRecursive(FS *fs, const String &dirPath);
  void handleFileUpload(AsyncWebServerRequest *request, String filename,
                        size_t index, uint8_t *data, size_t len, bool final);

  // Helper functions for filesystem operations
  void logFilesystemInfo(const String &fsName, size_t totalBytes,
                         size_t usedBytes);
  void printFilesystemInfo();
  String sanitizeFilename(const String &filename);
  void getStorageInfo(const String &fsParam, size_t &totalBytes,
                      size_t &usedBytes, size_t &freeBytes);
  bool writeFile(FS *filesystem, const String &filePath, const uint8_t *data,
                 size_t len);

  // Logging
  File logFile;
  bool serialStarted = false;
  int baudRate = 115200;
  int maxLogFileSize = 0;
  void cleanLogFile();
  void closeLogFile();
  void openLogFile();
  void logf(const char *format, ...);
  bool loggingStarted = false;
  String logFilePath = "/log";
  void startLogging(String filePath = "/log");
  void startSerial(int baudRate = 115200);
  String timestamp();
  String timestampForFilename();
  void writeLog(String message);
  void logError(String message);
  void logError(IPAddress ip) { logError(ip.toString()); }
  template <typename T> void logError(T value) { logError(String(value)); };
  void log(String message);
  void log(IPAddress ip) { log(ip.toString()); }
  template <typename T> void log(T value) { log(String(value)); };

  // Config
  void saveConfig();
  void readConfig();
  void printConfig();
  void handleConfig();
  JsonDocument defaultConfig();
  void mergeConfig(JsonDocument &json);

  // WiFi
  void startAP();
  void startWiFi();
  void startClient();
  int selectBestChannel();

  // mDNS
  void startMDNS();
#ifdef ESP8266
  void updateMDNS();
#endif

  // WebServer
  void srvAll();
  void srvOTA();
  void srvRoot();
  void srvInfo();
  void srvGPIO();
  void srvFiles();
  void srvConfig();
  void srvRestart();
  void initWebServer();
  void startWebServer();
  bool webServerStarted = false;
  void addCORS(AsyncWebServerResponse *response);
  void handleCorsPreflight(AsyncWebServerRequest *request);
  void sendJsonResponse(AsyncWebServerRequest *request, int statusCode,
                        const String &jsonBody);

  // LED Matrix
  void startLEDMatrix();

  // Spectral Analyzer
  void startSpectralAnalyzer();

  // I2C
  void scanI2CDevices();
  bool checkI2CDevice(uint8_t address);

  // BMI160
#ifdef ESPWiFi_BMI160_ENABLED
  float getTemperature(String unit = "C");
  int8_t readGyroscope(int16_t *gyroData);
  bool startBMI160(uint8_t address = 0x69);
  int8_t readAccelerometer(int16_t *accelData);
  void readGyroscope(float &x, float &y, float &z);
  void readAccelerometer(float &x, float &y, float &z);
#endif // ESPWiFi_BMI160_ENABLED

// Camera
#ifdef ESPWiFi_CAMERA_INSTALLED
  camera_config_t camConfig;
  WebSocket *camSoc = nullptr;
  bool checkCameraGPIOConflicts();
  bool initCamera();
  void startCamera();
  void recordCamera();
  void deinitCamera();
  void clearCameraBuffer();
  void cameraConfigHandler();
  void streamCamera(int frameRate = 10);
  void takeSnapshot(String filePath = "/snapshots/snapshot.jpg");

  // Camera operation safety
  bool cameraOperationInProgress = false;
#endif

  // RSSI
  WebSocket *rssiWebSocket = nullptr;
  void streamRSSI();

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String &filename);
  String bytesToHumanReadable(size_t bytes);
  void runAtInterval(unsigned int interval, unsigned long &lastIntervalRun,
                     std::function<void()> functionToRun);

  // OTA
  void startOTA();
  void handleOTAHtml(AsyncWebServerRequest *request);
  void handleOTAStart(AsyncWebServerRequest *request);
  void handleOTAUpdate(AsyncWebServerRequest *request, String filename,
                       size_t index, uint8_t *data, size_t len, bool final);
  void handleFSUpdate(AsyncWebServerRequest *request, String filename,
                      size_t index, uint8_t *data, size_t len, bool final);
  void resetOTAState();
  // OTA state variables (externally accessible)
  bool otaInProgress;
  size_t otaCurrentSize;
  size_t otaTotalSize;
  String otaErrorString;
};

#endif // ESPWiFi