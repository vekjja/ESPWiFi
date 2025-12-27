#ifndef ESPWiFi_H
#define ESPWiFi_H

// ESP-IDF Headers
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
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

  // WiFi event handler state
  SemaphoreHandle_t wifi_connect_semaphore = nullptr;
  bool wifi_connection_success = false;
  esp_event_handler_instance_t wifi_event_instance = nullptr;
  esp_event_handler_instance_t ip_event_instance = nullptr;

  // WiFi event handler methods
  esp_err_t registerWiFiHandlers();
  void unregisterWiFiHandlers();

  // WiFi event handler methods (called from static callbacks)
  bool waitForWiFiConnection(int timeout_ms, int check_interval_ms = 100);
  void wifi_event_handler(esp_event_base_t event_base, int32_t event_id,
                          void *event_data);
  void ip_event_handler(esp_event_base_t event_base, int32_t event_id,
                        void *event_data);

  // Static callbacks for ESP-IDF event system
  static void wifi_event_handler_static(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data);
  static void ip_event_handler_static(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data);

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
  int maxLogFileSize = -1;
  bool serialStarted = false;
  bool loggingStarted = false;
  std::string logFilePath = "/log";
  void startLogging(std::string filePath = "/log");
  void startSerial(int baudRate = 115200);
  std::string timestamp();
  void logConfigHandler();
  bool shouldLog(LogLevel level);
  std::string timestampForFilename();
  void writeLog(std::string message);
  std::string formatLog(const char *format, va_list args);
  // Log functions
  void log(LogLevel level, const char *format, ...);
  void log(LogLevel level, std::string message) {
    log(level, "%s", message.c_str());
  }
  template <typename T> void log(T value) {
    log(INFO, "%s", std::to_string(value).c_str());
  };

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
  std::string ipAddress();
  void setHostname(std::string hostname);

  // mDNS
  // void startMDNS();

  // WebServer
  void srvFS();
  void srvAll();
  void srvLog();
  void srvInfo();
  void srvGPIO();
  void srvAuth();
  void srvConfig();
  void srvWildcard();
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
  esp_err_t sendFileResponse(httpd_req_t *req, const std::string &filePath);
  void HTTPRoute(httpd_uri_t route);
  // Overload for inline handler definition - accepts function pointer or
  // captureless lambda
  template <typename Handler>
  void HTTPRoute(const char *uri, httpd_method_t method, Handler handler) {
    if (!webServer) {
      log(ERROR, "Cannot register route: web server not initialized");
      return;
    }
    httpd_uri_t route = {
        .uri = uri,
        .method = method,
        .handler =
            handler, // Works for function pointers and captureless lambdas
        .user_ctx = this};
    esp_err_t ret = httpd_register_uri_handler(webServer, &route);
    if (ret != ESP_OK) {
      log(ERROR, "Failed to register uri: %s, error: %s", uri,
          esp_err_to_name(ret));
      return;
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

  // I2C
  void scanI2CDevices();
  bool checkI2CDevice(uint8_t address);

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
inline void toLowerCase(std::string &s) {
  for (char &c : s)
    c = tolower(c);
}

#endif // ESPWiFi
