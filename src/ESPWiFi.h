#ifndef ESPWiFi_H
#define ESPWiFi_H

// ESP-IDF Headers
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ArduinoJson (compatible with ESP-IDF)
#include <ArduinoJson.h>

// IntervalTimer
#include <IntervalTimer.h>

// #ifdef ESPWiFi_CAMERA
// #include <esp_camera.h>
// #endif

// Forward declaration - WebSocket not yet ported to ESP-IDF
// class WebSocket;

// Log levels
enum LogLevel { ACCESS, DEBUG, INFO, WARNING, ERROR };

// Note: Arduino framework now provides String and IPAddress classes
// Arduino's IPAddress class is now available for use

// File handle wrapper
struct File {
  FILE *handle;
  std::string path;

  File() : handle(nullptr) {}
  File(FILE *h, const std::string &p) : handle(h), path(p) {}

  operator bool() const { return handle != nullptr; }
  void close() {
    if (handle) {
      fclose(handle);
      handle = nullptr;
    }
  }
  size_t write(const uint8_t *data, size_t len) {
    return handle ? fwrite(data, 1, len, handle) : 0;
  }
  // Read single byte (for ArduinoJson compatibility)
  int read() {
    if (!handle)
      return -1;
    int c = fgetc(handle);
    return c;
  }
  // Read multiple bytes
  size_t read(uint8_t *buffer, size_t len) {
    return handle ? fread(buffer, 1, len, handle) : 0;
  }
  size_t size() {
    if (!handle)
      return 0;
    long pos = ftell(handle);
    fseek(handle, 0, SEEK_END);
    long sz = ftell(handle);
    fseek(handle, pos, SEEK_SET);
    return sz;
  }
  bool available() { return handle && !feof(handle); }
};

class ESPWiFi {
private:
  std::string _version = "v0.1.0";

public:
  int connectTimeout = 27000;
  JsonDocument config = defaultConfig();
  void (*connectSubroutine)() = nullptr;
  std::string configFile = "/config.json";
  std::string version() { return _version; }

  // Device
  void start();
  void runSystem();
  void startDevice();

  // File System
  void initSDCard();
  void initLittleFS();
  bool sdCardInitialized = false;
  bool littleFsInitialized = false;
  std::string lfsMountPoint = "/lfs"; // LittleFS mount point
  bool deleteDirectoryRecursive(const std::string &dirPath);
  void handleFileUpload(void *req, const std::string &filename, size_t index,
                        uint8_t *data, size_t len, bool final);

  // Helper functions for filesystem operations
  void logFilesystemInfo(const std::string &fsName, size_t totalBytes,
                         size_t usedBytes);
  void printFilesystemInfo();
  std::string sanitizeFilename(const std::string &filename);
  void getStorageInfo(const std::string &fsParam, size_t &totalBytes,
                      size_t &usedBytes, size_t &freeBytes);
  bool writeFile(const std::string &filePath, const uint8_t *data, size_t len);
  bool isRestrictedSystemFile(const std::string &fsParam,
                              const std::string &filePath);

  // Logging
  void cleanLogFile();
  int baudRate = 115200;
  int maxLogFileSize = 0;
  bool serialStarted = false;
  bool loggingStarted = false;
  std::string logFilePath = "/log";
  void startLogging(std::string filePath = "/log");
  void startSerial(int baudRate = 115200);
  std::string timestamp();
  std::string timestampForFilename();
  void writeLog(std::string message);
  bool shouldLog(LogLevel level);
  std::string formatLog(const char *format, va_list args);
  void log(LogLevel level, const char *format, ...);
  void log(LogLevel level, std::string message) {
    log(level, "%s", message.c_str());
  }
  // void log(IPAddress ip) { log(INFO, "%s", ip.toString().c_str()); }
  template <typename T> void log(T value) {
    log(INFO, "%s", std::to_string(value).c_str());
  };
  void logConfigHandler();

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
  std::string macAddress();
  std::string getHostname();
  void setHostname(std::string hostname);

  // mDNS
  void startMDNS();

  // WebServer
  void srvAll();
  void srvOTA();
  void srvRoot();
  void srvInfo();
  void srvGPIO();
  void srvFiles();
  void srvConfig();
  void srvRestart();
  void srvAuth();
  void srvBluetooth();
  void srvLog();
  bool authEnabled();
  void startWebServer();
  std::string generateToken();
  bool webServerStarted = false;
  void addCORS(httpd_req_t *req);
  bool authorized(httpd_req_t *req);
  httpd_handle_t webServer = nullptr;
  void handleCorsPreflight(httpd_req_t *req);
  void sendJsonResponse(httpd_req_t *req, int statusCode,
                        const std::string &jsonBody);
  template <size_t N> void registerHTTPRoutes(httpd_uri_t (&routes)[N]) {
    for (size_t i = 0; i < N; i++) {
      httpd_register_uri_handler(webServer, &routes[i]);
    }
  }

  // Camera - not yet ported to ESP-IDF
  // #ifdef ESPWiFi_CAMERA
  //   camera_config_t camConfig;
  //   WebSocket *camSoc = nullptr;
  //   bool cameraOperationInProgress = false;
  //   bool initCamera();
  //   void startCamera();
  //   void deinitCamera();
  //   void streamCamera();
  //   void clearCameraBuffer();
  //   void cameraConfigHandler();
  //   void updateCameraSettings();
  //   void takeSnapshot(std::string filePath = "/snapshots/snapshot.jpg");
  // #endif

  // RSSI - not yet ported to ESP-IDF
  // WebSocket *rssiWebSocket = nullptr;
  void startRSSIWebSocket();
  void streamRSSI();

  // Utils
  void setMaxPower();
  std::string getContentType(std::string filename);
  std::string bytesToHumanReadable(size_t bytes);
  std::string getFileExtension(const std::string &filename);
  void runAtInterval(unsigned int interval, unsigned long &lastIntervalRun,
                     std::function<void()> functionToRun);

  // LED Matrix
  void startLEDMatrix();

  // Spectral Analyzer
  void startSpectralAnalyzer();

  // I2C
  void scanI2CDevices();
  bool checkI2CDevice(uint8_t address);

  // BMI160
#ifdef ESPWiFi_BMI160_ENABLED
  float getTemperature(std::string unit = "C");
  int8_t readGyroscope(int16_t *gyroData);
  bool startBMI160(uint8_t address = 0x69);
  int8_t readAccelerometer(int16_t *accelData);
  void readGyroscope(float &x, float &y, float &z);
  void readAccelerometer(float &x, float &y, float &z);
#endif // ESPWiFi_BMI160_ENABLED

  // OTA
  void startOTA();
  bool isOTAEnabled();
  void handleOTAStart(void *req);
  void handleOTAUpdate(void *req, const std::string &filename, size_t index,
                       uint8_t *data, size_t len, bool final);
  void handleOTAFileUpload(void *req, const std::string &filename, size_t index,
                           uint8_t *data, size_t len, bool final);
  void resetOTAState();
  bool otaInProgress;
  size_t otaCurrentSize;
  size_t otaTotalSize;
  std::string otaErrorString;
  std::string otaMD5Hash;

  // Bluetooth
  bool startBluetooth();
  void stopBluetooth();
  bool isBluetoothConnected();
  void scanBluetoothDevices();
  bool bluetoothStarted = false;
  void bluetoothConfigHandler();
  void checkBluetoothConnectionStatus();
};

// Helper functions for String conversion
inline const char *c_str(const std::string &s) { return s.c_str(); }
inline bool isEmpty(const std::string &s) { return s.empty(); }
inline void toLowerCase(std::string &s) {
  for (char &c : s)
    c = tolower(c);
}

#endif // ESPWiFi
