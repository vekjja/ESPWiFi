#ifndef ESPWiFi_H
#define ESPWiFi_H

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

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
#include "esp_err.h"
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

// Third-party (compatible with ESP-IDF)
#include <ArduinoJson.h>
#include <IntervalTimer.h>

// -----------------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------------

// NOTE: This stays as a plain enum to avoid a larger refactor across the .cpps.
enum LogLevel { VERBOSE, ACCESS, DEBUG, INFO, WARNING, ERROR };

// -----------------------------------------------------------------------------
// Main device/application class
// -----------------------------------------------------------------------------

class ESPWiFi {
public:
  // ---- Types ----------------------------------------------------------------
  // Route handler signature used by registerRoute(). The caller receives:
  // - this pointer (ESPWiFi instance)
  // - the ESP-IDF request
  // - a stable, pre-captured clientInfo string for access logging
  using RouteHandler = esp_err_t (*)(ESPWiFi *espwifi, httpd_req_t *req,
                                     const std::string &clientInfo);

  // ---- Basic helpers/state
  // ---------------------------------------------------
  std::string version() { return _version; }
  void yield(int ms = 1) { vTaskDelay(pdMS_TO_TICKS(ms)); }

  int connectTimeout = 27000;
  JsonDocument config = defaultConfig();
  void (*connectSubroutine)() = nullptr;
  std::string configFile = "/config.json";

  // Enable/disable automatic reconnect on STA disconnect events.
  void setWiFiAutoReconnect(bool enable) { wifi_auto_reconnect = enable; }
  // Current connection status tracked by the WiFi event handlers.
  bool isWiFiConnected() const { return wifi_connection_success; }

  // ---- Lifecycle
  // -------------------------------------------------------------
  void start();
  void runSystem();

  // ---- Filesystem
  // ------------------------------------------------------------
  void initSDCard();
  void deinitSDCard();
  void initLittleFS();
  bool sdCardInitialized = false;
  bool littleFsInitialized = false;
  std::string lfsMountPoint = "/lfs"; // LittleFS mount point
  std::string sdMountPoint = "/sd";   // SD card mount point (FATFS)
  void *sdCard = nullptr; // Opaque handle (SD card pointer from IDF)
  bool sdInitAttempted = false;
  esp_err_t sdInitLastErr = ESP_OK;
  bool sdInitNotConfiguredForTarget = false;

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
  bool writeFileAtomic(const std::string &filePath, const uint8_t *data,
                       size_t len);
  char *readFile(const std::string &filePath, size_t *outSize = nullptr);
  bool isRestrictedSystemFile(const std::string &fsParam,
                              const std::string &filePath);

  // Small helpers used by filesystem HTTP routes (implemented in Utils.cpp)
  bool fileExists(const std::string &fullPath);
  bool dirExists(const std::string &fullPath);
  bool mkDir(const std::string &fullPath);
  std::string getQueryParam(httpd_req_t *req, const char *key);

  // ---- Logging
  // ---------------------------------------------------------------
  void cleanLogFile();
  int maxLogFileSize = 0;
  bool serialStarted = false;
  int baudRate = 115200; // ESP-IDF console UART is usually pre-initialized
  bool loggingStarted = false;
  std::string logFilePath = "/log";
  bool logToSD = false; // When true, write logs to SD mount instead of LFS

  void startLogging(std::string filePath = "/log");
  void startSerial(int baudRate = 115200);
  int getSerialBaudRate();
  std::string timestamp();
  void logConfigHandler();
  bool shouldLog(LogLevel level);
  void writeLog(std::string message);

  void log(LogLevel level, const char *format, ...);
  void log(LogLevel level, std::string message) {
    log(level, "%s", message.c_str());
  }
  template <typename T> void log(T value) {
    log(INFO, "%s", std::to_string(value).c_str());
  }

  // ---- Config
  // ----------------------------------------------------------------
  void saveConfig();
  void saveConfig(JsonDocument &configToSave);
  void readConfig();
  void printConfig();
  void handleConfig();
  JsonDocument defaultConfig();
  JsonDocument mergeJson(const JsonDocument &base, const JsonDocument &updates);
  void
  requestConfigUpdate(); // Request deferred config apply+save from main task

  // ---- WiFi
  // ------------------------------------------------------------------
  void startAP();
  void startWiFi();
  void startClient();
  int selectBestChannel();
  std::string getHostname();
  std::string ipAddress();
  void setHostname(std::string hostname);

  // ---- HTTP server / routes
  // --------------------------------------------------
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
  void srvWildcard();

  // ---- Camera (stubs; ESP-IDF port TBD) -------------------------------------
#ifdef ESPWiFi_CAMERA
  bool initCamera();
  void startCamera();
  void deinitCamera();
  void streamCamera();
  void clearCameraBuffer();
  void cameraConfigHandler();
  void updateCameraSettings();
  void takeSnapshot(std::string filePath = "/snapshots/snapshot.jpg");
#endif

  // ---- RSSI (stub)
  // -----------------------------------------------------------
  void startRSSIWebSocket();
  void streamRSSI();

  // ---- Utilities
  // -------------------------------------------------------------
  void setMaxPower();
  std::string getStatusFromCode(int statusCode);
  std::string getContentType(std::string filename);
  std::string bytesToHumanReadable(size_t bytes);
  std::string getFileExtension(const std::string &filename);
  void runAtInterval(unsigned int interval, unsigned long &lastIntervalRun,
                     std::function<void()> functionToRun);
  bool matchPattern(std::string_view uri, std::string_view pattern);

  // ---- I2C
  // -------------------------------------------------------------------
  void scanI2CDevices();
  bool checkI2CDevice(uint8_t address);

  // ---- OTA
  // -------------------------------------------------------------------
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

  // ---- Bluetooth
  // -------------------------------------------------------------
  bool startBluetooth();
  void stopBluetooth();
  bool isBluetoothConnected();
  void scanBluetoothDevices();
  bool bluetoothStarted = false;
  void bluetoothConfigHandler();
  void checkBluetoothConnectionStatus();

private:
  // ---- Version
  // ---------------------------------------------------------------
  std::string _version = "v0.1.0";

  // ---- Route trampoline/state
  // ------------------------------------------------
  struct RouteCtx {
    ESPWiFi *self = nullptr;
    RouteHandler handler = nullptr;
  };
  std::vector<RouteCtx *> _routeContexts;
  static esp_err_t routeTrampoline(httpd_req_t *req);

  // ---- WiFi event handling
  // ---------------------------------------------------
  SemaphoreHandle_t wifi_connect_semaphore = nullptr;
  bool wifi_connection_success = false;
  bool wifi_auto_reconnect = true; // auto-reconnect on STA disconnect
  esp_event_handler_instance_t wifi_event_instance = nullptr;
  esp_event_handler_instance_t ip_event_instance = nullptr;

  // ---- Deferred config update (avoid heavy work in HTTP handlers)
  // ------------
  bool configNeedsUpdate = false;

  // ---- Log file synchronization (best-effort; avoid blocking httpd)
  // ----------
  SemaphoreHandle_t logFileMutex = nullptr;
  // NOTE: We intentionally use this mutex in a **best-effort / bounded-wait**
  // way for log file I/O (see `src/Log.cpp`):
  // - Pros: much fewer dropped *file* log lines vs fully non-blocking, while
  //   still keeping waits short so the ESP-IDF `httpd` task stays responsive.
  // - Cons: file logging can still drop lines under heavy contention (serial
  //   still prints), and even a short wait can add tiny latency if logging is
  //   called from request paths.
  //
  // If you want **blocking + precise file logging** (every log line is appended
  // to the file, in order), change the policy in `src/Log.cpp`:
  // - In `writeLog()`:
  //   - Replace the bounded wait (e.g. `pdMS_TO_TICKS(18)`) with a blocking
  //   take
  //     like `xSemaphoreTake(logFileMutex, portMAX_DELAY)` (or a longer bounded
  //     wait such as `pdMS_TO_TICKS(50)`).
  //   - Consider adding a `fflush()`/`fsync()` strategy if you need durability
  //     across power loss (higher latency + wear).
  // - In `logConfigHandler()` / `cleanLogFile()`:
  //   - Use a blocking take (or slightly longer bounded wait) so
  //   reconfiguration
  //     cannot race with file writes.
  //
  // Tradeoffs of blocking mode:
  // - Pros: deterministic file persistence (no skipped lines).
  // - Cons: can stall the `httpd` task during bursts, increasing perceived
  //   latency and watchdog risk if logging is done inside request paths.

  esp_err_t registerWiFiHandlers();
  void unregisterWiFiHandlers();
  bool waitForWiFiConnection(int timeout_ms, int check_interval_ms = 100);
  void wifi_event_handler(esp_event_base_t event_base, int32_t event_id,
                          void *event_data);
  void ip_event_handler(esp_event_base_t event_base, int32_t event_id,
                        void *event_data);

  static void wifi_event_handler_static(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data);
  static void ip_event_handler_static(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data);
};

// -----------------------------------------------------------------------------
// Small string helpers
// -----------------------------------------------------------------------------

inline void toLowerCase(std::string &s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
}

#endif // ESPWiFi_H
