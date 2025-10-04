#ifndef ESPWiFi_H
#define ESPWiFi_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
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

#ifdef ESPWiFi_CAMERA_ENABLED
#include <esp_camera.h>
#endif

// Forward declaration
class WebSocket;

class ESPWiFi {
private:
  FS *fs;
  bool sdCardStarted = false;
  bool loggingStarted = false;
  bool littleFsStarted = false;

public:
  JsonDocument config;
  int connectTimeout = 15000;
  String logFile = "/log.txt";
  String configFile = "/config.json";
  AsyncWebServer *webServer = nullptr;
  void (*connectSubroutine)() = nullptr;

  // File System
  void startSDCard();
  void startLittleFS();
  void listFiles(FS *fs);
  void readFile(FS *fs, const String &filePath);
  void deleteFile(FS *fs, const String &filePath);
  void writeFile(FS *fs, const String &filePath, const String &content);
  void appendToFile(FS *fs, const String &filePath, const String &content);
  bool fileExists(FS *fs, const String &filePath);
  bool dirExists(FS *fs, const String &dirPath);
  bool mkDir(FS *fs, const String &dirPath);

  // Power Management
  void stopSleep();
  void enablePowerSaving();
  void applyPowerSettings();
  void optimizeForFastResponse();

  // Log
  void closeLog();
  String timestamp();
  String timestampForFilename();
  void checkAndCleanupLogFile();
  void writeLog(String message);
  void logf(const char *format, ...);
  void startSerial(int baudRate = 115200);
  void startLog(String logFile = "/log.txt");

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
  void defaultConfig();
  void mergeConfig(JsonObject &json);
  void (*configUpdateCallback)() = nullptr;

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
  void srvLog();
  void srvOTA();
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
#ifdef ESPWiFi_CAMERA_ENABLED
  void startCamera();
  camera_config_t getCamConfig();
  void streamCamera(int frameRate = 10);
  void takeSnapshot(String filePath = "/snapshots/snapshot.jpg");
#endif

  // Microphone
  void startMicrophone();
  void streamMicrophone();

  // RSSI
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