#ifndef ESPWiFi_H
#define ESPWiFi_H

// ESP-IDF Configuration (must be first to provide CONFIG_* defines)
#include "sdkconfig.h"

// Standard library
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdio.h>
#include <string>
#include <string_view>
#include <vector>

// POSIX / libc
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ESP-IDF
#ifdef CONFIG_BT_A2DP_ENABLE
#include "esp_a2dp_api.h"
#endif
#ifdef ESPWiFi_CAMERA_INSTALLED
#include <esp_camera.h>
#endif
#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <ArduinoJson.h>
#include <IntervalTimer.h>

// Forward declarations for BLE types (to avoid pulling in NimBLE headers)
#ifdef CONFIG_BT_NIMBLE_ENABLED
struct ble_gap_event;
#endif

enum LogLevel { VERBOSE, ACCESS, DEBUG, INFO, WARNING, ERROR };

class ESPWiFi {
public:
  using RouteHandler = esp_err_t (*)(ESPWiFi *espwifi, httpd_req_t *req,
                                     const std::string &clientInfo);

  // ---- Basic helpers/state
  std::string version() { return _version; }
  void feedWatchDog(int ms = 10) { vTaskDelay(pdMS_TO_TICKS(ms)); }

  int connectTimeout = 27000;
  JsonDocument config = defaultConfig();
  void (*connectSubroutine)() = nullptr;
  std::string configFile = "/config.json";

  // Enable/disable automatic reconnect on STA disconnect events.
  void setWiFiAutoReconnect(bool enable) { wifiAutoReconnect = enable; }
  // Current connection status tracked by the WiFi event handlers.
  bool isWiFiConnected() const { return wifi_connection_success; }
  // Check if WiFi driver is initialized (regardless of connection status)
  bool isWiFiInitialized() const;

  // ---- Lifecycle
  void start();
  void runSystem();

  // ---- Filesystem
  void initSDCard();
  void initLittleFS();
  void initFilesystem() {
    initSDCard();
    initLittleFS();
  }
  bool checkSDCard();
  void deinitSDCard();
  void handleSDCardError();
  bool sdNotSupported = false;
  esp_err_t sdInitLastErr = ESP_OK;
  bool sdInitAttempted = false;
  void *sdCard = nullptr;
  void *lfs = nullptr;
  std::string lfsMountPoint = "/lfs";
  std::string sdMountPoint = "/sd";
  bool sdSpiBusOwned = false;
  int sdSpiHost = -1;
  IntervalTimer sdCardCheck = IntervalTimer(5000);

  bool deleteDirectoryRecursive(const std::string &dirPath);
  void handleFileUpload(void *req, const std::string &filename, size_t index,
                        uint8_t *data, size_t len, bool final);

  // Filesystem helpers
  void printFilesystemInfo();
  std::string sanitizeFilename(const std::string &filename);
  void logFilesystemInfo(const std::string &fsName, size_t totalBytes,
                         size_t usedBytes);
  void getStorageInfo(const std::string &fsParam, size_t &totalBytes,
                      size_t &usedBytes, size_t &freeBytes);
  bool writeFile(const std::string &filePath, const uint8_t *data, size_t len);
  char *readFile(const std::string &filePath, size_t *outSize = nullptr);
  bool isProtectedFile(const std::string &fsParam, const std::string &filePath);

  // Streaming file I/O operations (for large file uploads)
  FILE *openFileForWrite(const std::string &fullPath);
  bool writeFileChunk(FILE *f, const void *data, size_t len);
  bool closeFileStream(FILE *f, const std::string &fullPath);

  // Small helpers used by filesystem HTTP routes (implemented in Utils.cpp)
  bool fileExists(const std::string &fullPath);
  bool dirExists(const std::string &fullPath);
  bool mkDir(const std::string &fullPath);
  std::string getQueryParam(httpd_req_t *req, const char *key);

  // ---- Logging
  void cleanLogFile();
  int maxLogFileSize = 0;
  bool loggingStarted = false;
  std::string logFilePath = "/espwifi.log";
  // Helper to determine which filesystem to use for logging
  // Sets useSD and useLFS variables, returns true if a filesystem is available
  bool getLogFilesystem(bool &useSD, bool &useLFS);
  void startLogging();
  std::string timestamp();
  void logConfigHandler();
  bool shouldLog(LogLevel level);
  void writeLog(std::string message);

  // Core logging implementation - formats and writes the log message
  void logImpl(LogLevel level, const std::string &message);

  void log(LogLevel level, const char *format, ...);
  void log(LogLevel level, std::string message) { logImpl(level, message); }
  void log(LogLevel level, const char *format, const std::string &arg);
  template <typename T> void log(T value) {
    log(INFO, "%s", std::to_string(value).c_str());
  }

  // ---- Config
  void saveConfig();
  void readConfig();
  void handleConfigUpdate();
  void requestConfigSave();
  std::string prettyConfig();
  JsonDocument defaultConfig();
  void maskSensitiveFields(JsonVariant variant);
  JsonDocument mergeJson(const JsonDocument &base, const JsonDocument &updates);

  // ---- WiFi
  void initNVS();
  void startAP();
  void startWiFi();
  void startMDNS();
  void startClient();
  int selectBestChannel();
  std::string getHostname();
  std::string ipAddress();
  void setHostname(std::string hostname);

  // ---- HTTP server / routes
  void startWebServer();
  bool webServerStarted = false;
  httpd_handle_t webServer = nullptr;

  // Route registration helpers
  esp_err_t registerRoute(const char *uri, httpd_method_t method,
                          RouteHandler handler);

  // CORS + auth verification
  void addCORS(httpd_req_t *req);
  void handleCorsPreflight(httpd_req_t *req);
  esp_err_t verifyRequest(httpd_req_t *req,
                          std::string *outClientInfo = nullptr);
  bool authorized(httpd_req_t *req);
  bool isExcludedPath(const char *uri);
  bool authEnabled();
  std::string generateToken();

  // Response helpers
  const char *getMethodString(int method);
  std::string getClientInfo(httpd_req_t *req);
  esp_err_t sendJsonResponse(httpd_req_t *req, int statusCode,
                             const std::string &jsonBody,
                             const std::string *clientInfo = nullptr);
  esp_err_t sendFileResponse(httpd_req_t *req, const std::string &filePath,
                             const std::string *clientInfo = nullptr);
  void logAccess(int statusCode, const std::string &clientInfo,
                 size_t bytesSent = 0);
  JsonDocument readRequestBody(httpd_req_t *req);

  // Route groups
  void srvFiles();
  void srvAll();
  void srvLog();
  void srvInfo();
  void srvRoot();
  void srvGPIO();
  void srvAuth();
  void srvConfig();
  void srvBLE();
  void srvBluetooth();
  void srvWildcard();

  // ---- RSSI (stub)
  void startRSSIWebSocket();
  void streamRSSI();

  // ---- Power Management
  void powerConfigHandler();
  void applyWiFiPowerSettings();
  JsonDocument getWiFiPowerInfo();
  std::string getStatusFromCode(int statusCode);
  std::string getContentType(std::string filename);
  std::string bytesToHumanReadable(size_t bytes);
  std::string getFileExtension(const std::string &filename);
  bool matchPattern(std::string_view uri, std::string_view pattern);

  // ---- JSON helpers (implemented in Utils.cpp)
  void deepMerge(JsonVariant dst, JsonVariantConst src, int depth = 0);
  void runAtInterval(unsigned int interval, unsigned long &lastIntervalRun,
                     std::function<void()> functionToRun);

  // ---- I2C
  void scanI2CDevices();
  bool checkI2CDevice(uint8_t address);

  // ---- OTA
  void startOTA();
  bool isOTAEnabled();
  void handleOTAStart(void *req);
  void handleOTAUpdate(void *req, const std::string &filename, size_t index,
                       uint8_t *data, size_t len, bool final);
  void handleOTAFileUpload(void *req, const std::string &filename, size_t index,
                           uint8_t *data, size_t len, bool final);
  void resetOTAState();

  // OTA state (initialized defensively; OTA is currently stubbed)
  bool otaInProgress = false;
  size_t otaCurrentSize = 0;
  size_t otaTotalSize = 0;
  std::string otaErrorString;
  std::string otaMD5Hash;

  // ---- BLE Provisioning
#ifdef CONFIG_BT_NIMBLE_ENABLED
  void *ble = nullptr;
  bool startBLE();
  void deinitBLE();
  uint8_t getBLEStatus();
  std::string getBLEAddress();
  void bleConfigHandler();
  esp_err_t startBLEAdvertising();
#endif

  // ---- Bluetooth Audio
#ifdef CONFIG_BT_A2DP_ENABLE
  bool connectBluetoothed = false;
  bool btStarted = false;
  void bluetoothConfigHandler();
  void startBluetooth();
  void stopBluetooth();
  void scanBluetooth();
  void connectBluetooth(const std::string &address);
  esp_err_t RegisterBluetoothHandlers();
  void UnregisterBluetoothHandlers();
#endif

  // ---- Camera
#ifdef ESPWiFi_CAMERA_INSTALLED
  sensor_t *camera = nullptr;
  void srvCamera();
  bool initCamera();
  void deinitCamera();
  void clearCameraBuffer();
  void updateCameraSettings();
  void printCameraSettings();
  void cameraConfigHandler();
  void streamCamera();
  esp_err_t sendCameraSnapshot(httpd_req_t *req, const std::string &clientInfo);

  // Camera event handlers (instance methods)
  void cameraInitHandler(bool success, void *obj);
  void cameraSettingsUpdateHandler(void *obj);
  void cameraFrameCaptureHandler(uint32_t frameNumber, size_t frameSize,
                                 void *obj);
  void cameraErrorHandler(esp_err_t errorCode, const char *errorContext,
                          void *obj);

  // Camera handler registration
  esp_err_t registerCameraHandlers();
  void unregisterCameraHandlers();
#endif

private:
  std::string _version = "v0.1.0";

  // ---- Route trampoline/state
  struct RouteCtx {
    ESPWiFi *self = nullptr;
    RouteHandler handler = nullptr;
  };
  std::vector<RouteCtx *> _routeContexts;
  static esp_err_t routeTrampoline(httpd_req_t *req);

  // ---- WiFi event handling
  SemaphoreHandle_t wifi_connect_semaphore = nullptr;
  bool wifi_connection_success = false;
  bool wifiAutoReconnect = true; // auto-reconnect on STA disconnect
  esp_event_handler_instance_t wifi_event_instance = nullptr;
  esp_event_handler_instance_t ip_event_instance = nullptr;

  // ---- Deferred config operations (avoid heavy work in HTTP handlers)
  bool configNeedsSave = false;
  JsonDocument configUpdate; // Temporary storage for config updates
                             // from HTTP handler

  // ---- CORS cache (minimize per-request work/allocations)
  void corsConfigHandler();
  bool cors_cache_enabled = true;
  bool cors_cache_has_origins = false;
  bool cors_cache_allow_any_origin = true; // true when origins contains "*", or
                                           // when cors isn't configured
  std::string cors_cache_allow_methods;    // e.g. "GET, POST, PUT, OPTIONS"
  std::string cors_cache_allow_headers;    // e.g. "Content-Type, Authorization"

  // ---- Log file synchronization (best-effort; avoid blocking httpd)
  SemaphoreHandle_t logFileMutex = nullptr;

  // ---- WiFi event handlers
  esp_err_t registerWiFiHandlers();
  void unregisterWiFiHandlers();
  bool waitForWiFiConnection(int timeout_ms, int check_interval_ms = 100);
  void wifiEventHandler(esp_event_base_t event_base, int32_t event_id,
                        void *event_data);
  void ipEventHandler(esp_event_base_t event_base, int32_t event_id,
                      void *event_data);

  static void wifiEventHandlerStatic(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data);
  static void ipEventHandlerStatic(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data);

  // Bluetooth event handlers
#ifdef CONFIG_BT_A2DP_ENABLE
  void bluetoothConnectionSC(esp_a2d_connection_state_t state, void *obj);
  void btAudioStateChange(esp_a2d_audio_state_t state, void *obj);
  static void bluetoothConnectionSCStatic(esp_a2d_connection_state_t state,
                                          void *obj);
  static void btAudioStateChangeStatic(esp_a2d_audio_state_t state, void *obj);
#endif

  // BLE event handlers (instance methods)
#ifdef CONFIG_BT_NIMBLE_ENABLED
  void bleConnectionHandler(int status, uint16_t conn_handle, void *obj);
  void bleDisconnectionHandler(int reason, void *obj);
  void bleAdvertisingCompleteHandler(void *obj);
  void bleSubscribeHandler(uint16_t conn_handle, void *obj);
  void bleMtuUpdateHandler(uint16_t conn_handle, uint16_t mtu, void *obj);
  void bleHostSyncHandler(void *obj);
  void bleHostResetHandler(int reason, void *obj);
  void bleHostTaskStartedHandler(void *obj);

  // BLE static callbacks for NimBLE stack
  static int bleGapEventCallbackStatic(struct ble_gap_event *event, void *arg);
  static void bleHostSyncCallbackStatic(void *arg);
  static void bleHostResetCallbackStatic(int reason, void *arg);
  static void bleHostTaskStatic(void *arg);
#endif

  // Camera event handlers (static wrappers for callbacks)
#ifdef ESPWiFi_CAMERA_INSTALLED
  static void cameraInitHandlerStatic(bool success, void *obj);
  static void cameraSettingsUpdateHandlerStatic(void *obj);
  static void cameraFrameCaptureHandlerStatic(uint32_t frameNumber,
                                              size_t frameSize, void *obj);
  static void cameraErrorHandlerStatic(esp_err_t errorCode,
                                       const char *errorContext, void *obj);
#endif
};

// String helper
inline void toLowerCase(std::string &s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
}

#endif // ESPWiFi_H
