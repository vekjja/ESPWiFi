#ifndef ESPWiFi_H
#define ESPWiFi_H

// ESP-IDF Configuration (must be first to provide CONFIG_* defines)
#include "sdkconfig.h"

// Standard library
#include <stdio.h>

#include <array>
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// POSIX / libc
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ESP-IDF (esp_a2dp_api.h only exists when CONFIG_BT_A2DP_ENABLE is set in
// sdkconfig)
#if defined(CONFIG_BT_A2DP_ENABLE)
#include "BluetoothA2DPSource.h"
#include "esp_a2dp_api.h"
#endif

// SDCard model selection helper. This is safe to include even when no SD card
// model is selected (it will set ESPWiFi_SDCARD_MODEL_SELECTED=0 and pins=-1).
#include "SDCardPins.h"

// TFT model selection helper (safe when no TFT model is selected).
#include "TFTPins.h"

// Camera model selection helper. This is safe to include even when no camera
// model is selected (it will set ESPWiFi_CAMERA_MODEL_SELECTED=0 and pins=-1).
#include "CameraPins.h"

#if ESPWiFi_HAS_CAMERA
#include <esp_camera.h>
#endif

#include <ArduinoJson.h>
#include <IntervalTimer.h>
#include <WebSocket.h>

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

// Forward declarations for BLE types (to avoid pulling in NimBLE headers)
#ifdef CONFIG_BT_NIMBLE_ENABLED
struct ble_gap_event;
struct ble_gatt_access_ctxt;
#endif

enum LogLevel { VERBOSE, ACCESS, DEBUG, INFO, WARNING, ERROR };

class ESPWiFi {
 public:
  using RouteHandler = esp_err_t (*)(ESPWiFi* espwifi, httpd_req_t* req,
                                     const std::string& clientInfo);

  // ---- Basic helpers/state
  std::string version() { return _version; }
  void feedWatchDog(int ms = 10) { vTaskDelay(pdMS_TO_TICKS(ms)); }

  int connectTimeout = 15000;
  JsonDocument config = defaultConfig();
  JsonDocument oldConfig;
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

  // ---- Chunked file helper (used by control socket commands like get_logs)
  // Populates a response JSON with a chunk/tail of a file.
  void fillChunkedDataResponse(JsonDocument& resp, const std::string& fullPath,
                               const std::string& virtualPath,
                               const std::string& source, int64_t offset,
                               int tailBytes, int maxBytes);

  // ---- Filesystem
  void initSDCard();
  void initLittleFS();
  void initFilesystem() {
    initLittleFS();
    initSDCard();
  }
  bool checkSDCard();
  void deinitSDCard();
  void handleSDCardError();
  bool sdNotSupported = false;
  esp_err_t sdInitLastErr = ESP_OK;
  bool sdInitAttempted = false;
  void* sdCard = nullptr;
  void* lfs = nullptr;
  std::string lfsMountPoint = "/lfs";
  std::string sdMountPoint = "/sd";
  bool sdSpiBusOwned = false;
  int sdSpiHost = -1;
  IntervalTimer sdCardCheck = IntervalTimer(5000);
  // Avoid spamming logs/CPU when no SD is present: after a failed mount
  // attempt, wait before retrying.
  unsigned long sdNextRetryMs_ = 0;
  unsigned long sdRetryIntervalMs_ = 60000;

  // ---- TFT + Touch (optional; ESP-IDF esp_lcd based)
  void initTFT();
  void renderTFT();
  void playBootAnimation();
  /** Play MJPEG from file (path relative to SD or full path). Returns true if
   * >= 1 frame played. */
  bool playMJPG(const std::string& filepath);
  /** Set from your app (e.g. in main). Called by the library after ui_init()
   * so you can register LVGL event handlers. Pass your ESPWiFi* as user_data.
   */
  std::function<void(ESPWiFi*)> registerUiEventHandlers;
#if ESPWiFi_HAS_TFT
  // Public accessors for TFT internals (useful for other modules).
  // These are intentionally untyped (void*) to avoid pulling esp_lcd headers
  // into the public header.
  bool isTftInitialized() const { return tftInitialized; }
  void* tftPanelHandle() const { return tftPanel_; }  // esp_lcd_panel_handle_t
  void* tftPanelIoHandle() const {
    return tftPanelIo_;
  }  // esp_lcd_panel_io_handle_t
  void* tftSpiBusHandle() const {
    return tftSpiBus_;
  }  // esp_lcd_spi_bus_handle_t
  bool tftInitialized = false;
  IntervalTimer tftRefresh = IntervalTimer(250);  // UI refresh cadence
  void* tftSpiBus_ = nullptr;                     // esp_lcd_spi_bus_handle_t
  void* tftPanelIo_ = nullptr;                    // esp_lcd_panel_io_handle_t
  void* tftPanel_ = nullptr;                      // esp_lcd_panel_handle_t
  void* tftTouch_ = nullptr;                      // esp_lcd_touch_handle_t
  bool tftBacklightOn_ = true;
  int64_t tftLastTouchMs_ = 0;
#endif

  bool deleteDirectoryRecursive(const std::string& dirPath);
  void handleFileUpload(void* req, const std::string& filename, size_t index,
                        uint8_t* data, size_t len, bool final);

  // Filesystem helpers
  void printFilesystemInfo();
  std::string sanitizeFilename(const std::string& filename);
  void logFilesystemInfo(const std::string& fsName, size_t totalBytes,
                         size_t usedBytes);
  void getStorageInfo(const std::string& fsParam, size_t& totalBytes,
                      size_t& usedBytes, size_t& freeBytes);
  bool writeFile(const std::string& filePath, const uint8_t* data, size_t len);
  char* readFile(const std::string& filePath, size_t* outSize = nullptr);
  bool isProtectedFile(const std::string& fsParam, const std::string& filePath);

  // Streaming file I/O operations (for large file uploads)
  FILE* openFileForWrite(const std::string& fullPath);
  bool writeFileChunk(FILE* f, const void* data, size_t len);
  bool closeFileStream(FILE* f, const std::string& fullPath);

  // Small helpers used by filesystem HTTP routes (implemented in Utils.cpp)
  bool fileExists(const std::string& fullPath);
  bool dirExists(const std::string& fullPath);
  bool mkDir(const std::string& fullPath);
  /** If path is not already under sdMountPoint, prepends it (for SD card
   * paths). */
  std::string resolvePathOnSD(const std::string& path);
  std::string getQueryParam(httpd_req_t* req, const char* key);
  std::string urlDecode(const std::string& encoded);

  // ---- Logging
  std::string formatIDFtoESPWiFi(const std::string& line,
                                 LogLevel* outLevel = nullptr);
  static int idfLogVprintfHook(const char* format, va_list args);

  void cleanLogFile();
  int maxLogFileSize = 0;
  bool loggingStarted = false;
  std::string logFilePath = "/espwifi.log";
  // Helper to determine which filesystem to use for logging
  // Sets useSD and useLFS variables, returns true if a filesystem is available
  bool getLogFilesystem(bool& useSD, bool& useLFS);
  void startLogging();
  std::string timestamp();
  void logConfigHandler();
  bool shouldLog(LogLevel level);
  void writeLog(std::string message);
  std::string logLevelToString(LogLevel level);

  // Core logging implementation - formats and writes the log message
  void logImpl(LogLevel level, const std::string& message);

  void log(LogLevel level, const char* format, ...);
  void log(LogLevel level, const char* format, const std::string& arg);
  void log(LogLevel level, std::string message) { logImpl(level, message); }
  template <typename T>
  void log(T value) {
    log(INFO, "%s", std::to_string(value).c_str());
  }

  // ---- Config
  bool saveConfig();
  void readConfig();
  void handleConfigUpdate();
  void requestConfigSave();
  std::string prettyConfig();
  JsonDocument defaultConfig();
  void maskSensitiveFields(JsonVariant variant);
  JsonDocument mergeJson(const JsonDocument& base, const JsonDocument& updates);
  // Queue a config update to be applied on the main loop (watchdog-safe).
  // The update is deep-merged into the current config and persisted.
  bool queueConfigUpdate(JsonVariantConst updates);

  // ---- Info
  // When yieldForWatchdog is true, yields briefly between heavier sections.
  JsonDocument buildInfoJson(bool yieldForWatchdog = true);

  // ---- WiFi
  void initNVS();
  void startAP();
  void startWiFi();
  void stopWiFi();
  void toggleWiFi();
  void startMDNS();
  void stopMDNS();
  void startClient();
  int selectBestChannel();
  void wifiConfigHandler();
  std::string getHostname();
  std::string genHostname();
  std::string ipAddress();
  std::string getMacAddress();
  void setHostname(std::string hostname);

  // ---- HTTP Web Server
  void startWebServer();
  void stopWebServer();
  bool webServerStarted = false;
  httpd_handle_t webServer = nullptr;

  // ---- HTTP Web Server Routes
  void srvAuth();
  void srvRoot();
  void srvWildcard();

  // ---- HTTPS/TLS credential management
  // TLS materials are kept in-memory for the lifetime of the HTTPS server,
  // because esp_https_server expects the cert/key buffers to remain valid.
  bool setTlsServerCredentials(const char* certPem, size_t certPemLen,
                               const char* keyPem, size_t keyPemLen);
  void clearTlsServerCredentials();

  // Route registration helpers
  esp_err_t registerRoute(const char* uri, httpd_method_t method,
                          RouteHandler handler);

  // CORS + auth verification
  void addCORS(httpd_req_t* req);
  void handleCorsPreflight(httpd_req_t* req);
  esp_err_t verifyRequest(httpd_req_t* req,
                          std::string* outClientInfo = nullptr);
  bool authorized(httpd_req_t* req);
  bool isExcludedPath(const char* uri);
  bool authEnabled();
  std::string generateToken();

  // Response helpers
  const char* getMethodString(int method);
  std::string getClientInfo(httpd_req_t* req);
  esp_err_t sendJsonResponse(httpd_req_t* req, int statusCode,
                             const std::string& jsonBody,
                             const std::string* clientInfo = nullptr);
  esp_err_t sendFileResponse(httpd_req_t* req, const std::string& filePath,
                             const std::string* clientInfo = nullptr);
  void logAccess(int statusCode, const std::string& clientInfo,
                 size_t bytesSent = 0);
  JsonDocument readRequestBody(httpd_req_t* req);

  // GPIO helper methods (used by both HTTP and WebSocket)
  bool setGPIO(int pin, bool state, std::string& errorMsg);
  bool getGPIO(int pin, int& state, std::string& errorMsg);
  bool setPWM(int pin, int duty, int freq, std::string& errorMsg);

  // File browser helper methods (used by both HTTP and WebSocket)
  bool listFiles(const std::string& fs, const std::string& path,
                 JsonDocument& outDoc, std::string& errorMsg);
  bool makeDirectory(const std::string& fs, const std::string& path,
                     const std::string& name, std::string& errorMsg);
  bool renameFile(const std::string& fs, const std::string& oldPath,
                  const std::string& newName, std::string& errorMsg);
  bool deleteFile(const std::string& fs, const std::string& path,
                  std::string& errorMsg);

  // ---- Control WebSocket (LAN)
  void startControlWebSocket();
  void stopControlWebSocket();

  // ---- Media WebSocket (for LAN media; camera streaming on request)
  void startMediaWebSocket();
  void stopMediaWebSocket();

  // ---- Power Management
  void powerConfigHandler();
  void applyWiFiPowerSettings();
  JsonDocument getWiFiPowerInfo();
  // ---- Utils
  std::string getStatusFromCode(int statusCode);
  std::string getContentType(std::string filename);
  std::string bytesToHumanReadable(size_t bytes);
  std::string getFileExtension(const std::string& filename);
  bool matchPattern(std::string_view uri, std::string_view pattern);
  void deepMerge(JsonVariant dst, JsonVariantConst src, int depth = 0);
  void runAtInterval(unsigned int interval, unsigned long& lastIntervalRun,
                     std::function<void()> functionToRun);

  // ---- I2C
  void scanI2CDevices();
  bool checkI2CDevice(uint8_t address);

  // ---- OTA
  void startOTA();
  bool isOTAEnabled();
  void handleOTAStart(void* req);
  void handleOTAUpdate(void* req, const std::string& filename, size_t index,
                       uint8_t* data, size_t len, bool final);
  void handleOTAFileUpload(void* req, const std::string& filename, size_t index,
                           uint8_t* data, size_t len, bool final);
  void resetOTAState();

  // OTA state (initialized defensively; OTA is currently stubbed)
  bool otaInProgress = false;
  size_t otaCurrentSize = 0;
  size_t otaTotalSize = 0;
  std::string otaErrorString;
  std::string otaMD5Hash;

  // ---- BLE Provisioning
  bool startBLE();
  void bleConfigHandler();
#ifdef CONFIG_BT_NIMBLE_ENABLED
  using BleAccessCallback = int (*)(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt* ctxt,
                                    void* arg);

  // Route-style BLE characteristic handler.
  void* ble = nullptr;
  void deinitBLE();
  uint8_t getBLEStatus();
  std::string getBLEAddress();
  esp_err_t startBLEAdvertising();

  // ---- BLE GATT registry (registerRoute-style API)
  void startBLEServices();
  bool registerBleService16(uint16_t svcUuid16);
  bool unregisterBleService16(uint16_t svcUuid16);
  bool addBleCharacteristic16(uint16_t svcUuid16, uint16_t chrUuid16,
                              uint16_t flags, BleAccessCallback accessCb,
                              void* arg = nullptr, uint8_t minKeySize = 0);

  void clearBleServices();
  bool applyBleServiceRegistry(bool restartNow = true);
#endif

  // ---- Bluetooth Audio
#ifdef CONFIG_BT_A2DP_ENABLE
  std::function<bool(const char* name, esp_bd_addr_t address, int rssi)>
      onBluetoothDeviceDiscovered;
  std::function<void(esp_a2d_connection_state_t state)>
      onBluetoothConnectionStateChanged;
  std::vector<std::string> bluetoothScannedHosts = {};
  void startBluetoothMp3Playback(const char* path);
  void startBluetoothWavPlayback(const char* path);
  BluetoothA2DPSource* a2dp_source = nullptr;
  std::string bluetoothConnectTargetName;
  void stopBluetoothMp3Playback();
  void stopBluetoothWavPlayback();
  void toggleBluetooth();
  void startBluetooth();
  void stopBluetooth();

  // ---- A2DP audio playback state
  volatile bool btAudioPlaying = false;
  TaskHandle_t btAudioTask = nullptr;
  std::string btAudioFilePath;
#endif

#ifdef CONFIG_HTTPD_WS_SUPPORT
  WebSocket ctrlSoc;
  bool ctrlSocStarted = false;

  // ---- Media socket (camera + audio + future binary streams)
  WebSocket mediaSoc;
  bool mediaSocStarted = false;

  // ---- Camera
  void streamCamera();
#if ESPWiFi_HAS_CAMERA
  sensor_t* camera = nullptr;
  void printCameraSettings();

  // Media camera streaming subscribers (clients that requested camera_start)
  static constexpr size_t kMaxMediaCameraStreamSubscribers = 8;
  int mediaCameraStreamSubFds_[kMaxMediaCameraStreamSubscribers] = {0};
  volatile size_t mediaCameraStreamSubCount_ = 0;

  void setMediaCameraStreamSubscribed(int clientFd, bool enable);
  void clearMediaCameraStreamSubscribed(int clientFd);
#endif  // ESPWiFi_HAS_CAMERA
#endif  // CONFIG_HTTPD_WS_SUPPORT

  // Camera API (always declared; stubs compile when camera is disabled)
  bool initCamera();
  void deinitCamera();
  void clearCameraBuffer();
  void updateCameraSettings();
  void cameraConfigHandler();
  esp_err_t sendCameraSnapshot(httpd_req_t* req, const std::string& clientInfo);
  bool takeSnapshot(bool save, std::string& url, std::string& errorMsg);

  // Camera event handlers (instance methods)
  void cameraInitHandler(bool success, void* obj);
  void cameraSettingsUpdateHandler(void* obj);
  void cameraFrameCaptureHandler(uint32_t frameNumber, size_t frameSize,
                                 void* obj);
  void cameraErrorHandler(esp_err_t errorCode, const char* errorContext,
                          void* obj);

  // Camera handler registration
  esp_err_t registerCameraHandlers();
  void unregisterCameraHandlers();

  unsigned long millis() {
    return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
  }

 private:
  std::string _version = "v0.1.0";

  // ---- HTTPS/TLS server state
  //
  // When TLS credentials are loaded from LittleFS, we keep them in-memory for
  // the lifetime of the web server, because esp_https_server expects the cert
  // and key buffers to remain valid.
  bool tlsServerEnabled_ = false;
  uint16_t webServerPort_ = 80;
  std::string tlsServerCertPem_;
  std::string tlsServerKeyPem_;

  // ---- Route trampoline/state
  struct RouteCtx {
    ESPWiFi* self = nullptr;
    RouteHandler handler = nullptr;
  };
  std::vector<RouteCtx*> _routeContexts;
  static esp_err_t routeTrampoline(httpd_req_t* req);

  // ---- WiFi event handling
  SemaphoreHandle_t wifi_connect_semaphore = nullptr;
  bool wifi_connection_success = false;
  bool wifiAutoReconnect = true;  // auto-reconnect on STA disconnect
  esp_event_handler_instance_t wifi_event_instance = nullptr;
  esp_event_handler_instance_t ip_event_instance = nullptr;

  // ---- Deferred config operations (avoid heavy work in HTTP handlers)
  bool configNeedsSave = false;
  JsonDocument configUpdate;  // Temporary storage for config updates
                              // from HTTP handler
  bool wifiRestartRequested_ = false;

  // ---- CORS cache (minimize per-request work/allocations)
  void corsConfigHandler();
  bool cors_cache_enabled = true;
  bool cors_cache_has_origins = false;
  bool cors_cache_allow_any_origin = true;  // true when origins contains "*",
                                            // or when cors isn't configured
  std::string cors_cache_allow_methods;     // e.g. "GET, POST, PUT, OPTIONS"
  std::string cors_cache_allow_headers;  // e.g. "Content-Type, Authorization"

  // ---- Log file synchronization (best-effort; avoid blocking httpd)
  SemaphoreHandle_t logFileMutex = nullptr;
  SemaphoreHandle_t deferredLogMutex = nullptr;

  // ---- WiFi event handlers
  esp_err_t registerWiFiHandlers();
  void unregisterWiFiHandlers();
  void wifiEventHandler(esp_event_base_t event_base, int32_t event_id,
                        void* event_data);
  void ipEventHandler(esp_event_base_t event_base, int32_t event_id,
                      void* event_data);

  static void wifiEventHandlerStatic(void* arg, esp_event_base_t event_base,
                                     int32_t event_id, void* event_data);
  static void ipEventHandlerStatic(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data);

  // BLE event handlers (instance methods)
#ifdef CONFIG_BT_NIMBLE_ENABLED
  void bleConnectionHandler(int status, uint16_t conn_handle, void* obj);
  void bleDisconnectionHandler(int reason, void* obj);
  void bleAdvertisingCompleteHandler(void* obj);
  void bleSubscribeHandler(uint16_t conn_handle, void* obj);
  void bleMtuUpdateHandler(uint16_t conn_handle, uint16_t mtu, void* obj);
  void bleHostSyncHandler(void* obj);
  void bleHostResetHandler(int reason, void* obj);
  void bleHostTaskStartedHandler(void* obj);

  // BLE static callbacks for NimBLE stack
  static int bleGapEventCallbackStatic(struct ble_gap_event* event, void* arg);
  static void bleHostSyncCallbackStatic(void* arg);
  static void bleHostResetCallbackStatic(int reason, void* arg);
  static void bleHostTaskStatic(void* arg);
#endif

  // Camera event handlers (static wrappers for callbacks)
  static void cameraInitHandlerStatic(bool success, void* obj);
  static void cameraSettingsUpdateHandlerStatic(void* obj);
  static void cameraFrameCaptureHandlerStatic(uint32_t frameNumber,
                                              size_t frameSize, void* obj);
  static void cameraErrorHandlerStatic(esp_err_t errorCode,
                                       const char* errorContext, void* obj);
};

// String helper
inline void toLowerCase(std::string& s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
}

#endif  // ESPWiFi_H
