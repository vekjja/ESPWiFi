/**
 * @file Camera.cpp
 * @brief Camera subsystem implementation for ESPWiFi
 *
 * This module provides camera initialization, configuration, and streaming
 * capabilities using the ESP-IDF camera driver (esp_camera). It supports
 * multiple camera modules (AI-Thinker, M5Stack, XIAO, etc.) with runtime
 * configuration of image quality, frame rate, and sensor settings.
 *
 * Key features:
 * - WebSocket-based JPEG frame streaming
 * - HTTP snapshot endpoint for single-frame capture
 * - Dynamic configuration (brightness, contrast, exposure, etc.)
 * - PSRAM/DRAM adaptive buffering
 * - Mutex-protected concurrent access
 * - Watchdog-safe chunked transfers
 *
 * @note Camera functionality requires ESPWiFi_CAMERA_ENABLED build flag
 * @note Requires CAMERA_MODEL_* to be defined (e.g., CAMERA_MODEL_XIAO_ESP32S3)
 * @note WebSocket streaming requires CONFIG_HTTPD_WS_SUPPORT
 */

// Only compile if camera is explicitly enabled AND a camera model is selected
#if defined(ESPWiFi_CAMERA_ENABLED) &&                                         \
    (defined(CAMERA_MODEL_WROVER_KIT) || defined(CAMERA_MODEL_ESP_EYE) ||      \
     defined(CAMERA_MODEL_M5STACK_PSRAM) ||                                    \
     defined(CAMERA_MODEL_M5STACK_V2_PSRAM) ||                                 \
     defined(CAMERA_MODEL_M5STACK_WIDE) ||                                     \
     defined(CAMERA_MODEL_M5STACK_ESP32CAM) ||                                 \
     defined(CAMERA_MODEL_M5STACK_UNITCAM) ||                                  \
     defined(CAMERA_MODEL_AI_THINKER) ||                                       \
     defined(CAMERA_MODEL_TTGO_T_JOURNAL) ||                                   \
     defined(CAMERA_MODEL_XIAO_ESP32S3) ||                                     \
     defined(CAMERA_MODEL_ESP32_CAM_BOARD) ||                                  \
     defined(CAMERA_MODEL_ESP32S3_CAM_LCD) ||                                  \
     defined(CAMERA_MODEL_ESP32S2_CAM_BOARD) ||                                \
     defined(CAMERA_MODEL_ESP32S3_EYE))

#include "ESPWiFi.h"
#include "IntervalTimer.h"
#include "WebSocket.h"

#include <CameraPins.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <sdkconfig.h>

// ============================================================================
// Constants and Module TAG
// ============================================================================

static const char *CAM_TAG = "ESPWiFi_Camera";

// Frame rate constraints (to keep streaming safe and watchdog-friendly)
#define CAM_MIN_FPS 1
#define CAM_MAX_FPS 15
#define CAM_DEFAULT_FPS 10

// Mutex timeout defaults (in milliseconds)
#define CAM_MUTEX_TIMEOUT_INIT_MS 200
#define CAM_MUTEX_TIMEOUT_FRAME_MS 60
#define CAM_MUTEX_TIMEOUT_QUICK_MS 15

// ============================================================================
// Camera Runtime State
// ============================================================================
// Note: These are kept file-static to avoid polluting ESPWiFi.h with
// esp_camera types and to provide encapsulation of camera subsystem state.

static WebSocket s_camWs;                ///< WebSocket for camera streaming
static bool s_camWsStarted = false;      ///< WebSocket initialization flag
static bool s_cameraInitialized = false; ///< Camera hardware init flag
static SemaphoreHandle_t s_camMutex = nullptr; ///< Mutex for concurrent access

// ============================================================================
// Camera Mutex Helpers
// ============================================================================

/**
 * @brief Acquire camera mutex with timeout
 *
 * Lazily creates the mutex on first call. Uses a reasonable timeout to
 * prevent indefinite blocking of HTTP request handlers or streaming tasks.
 *
 * @param ticks Timeout in FreeRTOS ticks (default: 60ms)
 * @return true if mutex acquired, false on timeout or creation failure
 */
static bool
takeCamMutex_(TickType_t ticks = pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_FRAME_MS)) {
  // Lazy initialization of mutex
  if (s_camMutex == nullptr) {
    s_camMutex = xSemaphoreCreateMutex();
    if (s_camMutex == nullptr) {
      ESP_LOGE(CAM_TAG, "Failed to create camera mutex");
      return false;
    }
  }

  if (xSemaphoreTake(s_camMutex, ticks) != pdTRUE) {
    ESP_LOGW(CAM_TAG, "Camera mutex timeout after %lu ms",
             (unsigned long)(ticks * portTICK_PERIOD_MS));
    return false;
  }

  return true;
}

/**
 * @brief Release camera mutex
 *
 * Safe to call even if mutex doesn't exist (no-op in that case).
 */
static void giveCamMutex_() {
  if (s_camMutex != nullptr) {
    xSemaphoreGive(s_camMutex);
  }
}

// ============================================================================
// Camera Initialization and Lifecycle
// ============================================================================

/**
 * @brief Initialize camera hardware and driver
 *
 * Configures the ESP camera driver with appropriate settings based on:
 * - Available PSRAM (higher resolution if present)
 * - Camera model pin configuration (from CameraPins.h)
 * - User configuration (frame rate, quality)
 *
 * This function is idempotent and thread-safe. Multiple calls return quickly
 * if already initialized. Uses a re-entrancy guard to prevent concurrent
 * initialization attempts.
 *
 * @return true if camera is initialized (or was already), false on failure
 *
 * @note Does not abort on failure per ESP32 robustness best practices
 * @note Safe to call from HTTP handlers or configuration update tasks
 */
bool ESPWiFi::initCamera() {
  // Fast path: already initialized
  if (s_cameraInitialized) {
    ESP_LOGD(CAM_TAG, "Camera already initialized");
    return true;
  }

  // Re-entrancy guard: camera init can be called from multiple contexts
  // (config handler, HTTP route, startup sequence)
  static bool initInProgress = false;
  if (initInProgress) {
    ESP_LOGW(CAM_TAG, "Camera initialization already in progress");
    return false;
  }
  initInProgress = true;

  log(INFO, "📷 Initializing Camera");
  ESP_LOGI(CAM_TAG, "Starting camera initialization");

  // Acquire mutex with extended timeout for init operations
  if (!takeCamMutex_(pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_INIT_MS))) {
    log(ERROR, "📷 Camera mutex timeout during init");
    ESP_LOGE(CAM_TAG, "Failed to acquire camera mutex");
    initInProgress = false;
    return false;
  }

  // Check if driver has sensor already (shouldn't happen, but be defensive)
  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {
    ESP_LOGW(CAM_TAG, "Camera sensor already exists in driver");
    s_cameraInitialized = true;
    giveCamMutex_();
    initInProgress = false;
    return true;
  }

  // Configure camera hardware settings
  camera_config_t cam = {};

  // LEDC peripheral for camera clock generation
  cam.ledc_channel = LEDC_CHANNEL_0;
  cam.ledc_timer = LEDC_TIMER_0;

  // Pin configuration from CameraPins.h (based on CAMERA_MODEL_* build flag)
  // These mappings are specific to each camera module/board variant
  cam.pin_d0 = Y2_GPIO_NUM;         // Data bit 0
  cam.pin_d1 = Y3_GPIO_NUM;         // Data bit 1
  cam.pin_d2 = Y4_GPIO_NUM;         // Data bit 2
  cam.pin_d3 = Y5_GPIO_NUM;         // Data bit 3
  cam.pin_d4 = Y6_GPIO_NUM;         // Data bit 4
  cam.pin_d5 = Y7_GPIO_NUM;         // Data bit 5
  cam.pin_d6 = Y8_GPIO_NUM;         // Data bit 6
  cam.pin_d7 = Y9_GPIO_NUM;         // Data bit 7
  cam.pin_xclk = XCLK_GPIO_NUM;     // External clock
  cam.pin_pclk = PCLK_GPIO_NUM;     // Pixel clock
  cam.pin_vsync = VSYNC_GPIO_NUM;   // Vertical sync
  cam.pin_href = HREF_GPIO_NUM;     // Horizontal reference
  cam.pin_sccb_sda = SIOD_GPIO_NUM; // I2C data (SCCB protocol)
  cam.pin_sccb_scl = SIOC_GPIO_NUM; // I2C clock (SCCB protocol)
  cam.pin_pwdn = PWDN_GPIO_NUM;     // Power down pin
  cam.pin_reset = RESET_GPIO_NUM;   // Reset pin

  // Clock frequency: 20MHz is conservative and widely compatible
  // Some sensors support up to 24MHz, but 20MHz provides better stability
  cam.xclk_freq_hz = 20000000;

  // Output format: JPEG for efficient wireless transmission
  cam.pixel_format = PIXFORMAT_JPEG;

  ESP_LOGI(CAM_TAG, "Pin configuration loaded for camera model");

  // Adaptive buffering based on PSRAM availability
  // PSRAM allows larger frames and multiple buffers for smoother streaming
  // ESP-IDF: Use esp_psram_is_initialized() instead of Arduino's psramFound()
  const bool usingPSRAM = esp_psram_is_initialized();
  if (usingPSRAM) {
    cam.frame_size = FRAMESIZE_SVGA;      // 800x600
    cam.jpeg_quality = 15;                // Lower = better quality (0-63)
    cam.fb_count = 2;                     // Double buffering
    cam.fb_location = CAMERA_FB_IN_PSRAM; // Store in external RAM
    cam.grab_mode = CAMERA_GRAB_LATEST;   // Discard old frames

    ESP_LOGI(CAM_TAG, "Using PSRAM: frame_size=SVGA, quality=15, buffers=2");
  } else {
    // Without PSRAM, use smaller frames and single buffer
    cam.frame_size = FRAMESIZE_QVGA;     // 320x240
    cam.jpeg_quality = 25;               // Lower quality for size
    cam.fb_count = 1;                    // Single buffer
    cam.fb_location = CAMERA_FB_IN_DRAM; // Internal RAM only
    cam.grab_mode = CAMERA_GRAB_LATEST;

    ESP_LOGI(CAM_TAG, "No PSRAM: frame_size=QVGA, quality=25, buffers=1");
  }

  // Extract frame rate from configuration (with bounds checking)
  int frameRate = config["camera"]["frameRate"].isNull()
                      ? CAM_DEFAULT_FPS
                      : config["camera"]["frameRate"].as<int>();

  // Clamp to safe range
  if (frameRate < CAM_MIN_FPS) {
    ESP_LOGW(CAM_TAG, "Frame rate %d too low, clamping to %d", frameRate,
             CAM_MIN_FPS);
    frameRate = CAM_MIN_FPS;
  }
  if (frameRate > CAM_MAX_FPS) {
    ESP_LOGW(CAM_TAG, "Frame rate %d too high, clamping to %d", frameRate,
             CAM_MAX_FPS);
    frameRate = CAM_MAX_FPS;
  }

  ESP_LOGI(CAM_TAG, "Target frame rate: %d FPS", frameRate);

  // Initialize the camera driver
  // Note: We do NOT use ESP_ERROR_CHECK here to avoid aborting on failure
  // [[memory:12698303]]
  esp_err_t err = esp_camera_init(&cam);
  if (err != ESP_OK) {
    const char *errName = esp_err_to_name(err);
    log(ERROR, "📷 Camera Init Failed: %s (0x%x)", errName, err);
    ESP_LOGE(CAM_TAG, "esp_camera_init() failed: %s (0x%x)", errName, err);

    // Best-effort cleanup
    (void)esp_camera_deinit();

    giveCamMutex_();
    initInProgress = false;
    return false;
  }

  // Apply user configuration settings (brightness, contrast, etc.)
  updateCameraSettings();

  s_cameraInitialized = true;
  giveCamMutex_();
  initInProgress = false;

  log(INFO, "📷 Camera initialized (%s, %d FPS)", usingPSRAM ? "PSRAM" : "DRAM",
      frameRate);
  ESP_LOGI(CAM_TAG, "Camera initialization complete");

  return true;
}

/**
 * @brief Start camera WebSocket endpoint
 *
 * Initializes the WebSocket server for streaming JPEG frames to connected
 * clients. Each client connection must provide authentication via URL token
 * parameter (?token=...).
 *
 * The WebSocket endpoint serves binary messages where each message is a
 * complete JPEG frame. Clients should decode these as images.
 *
 * @note Requires CONFIG_HTTPD_WS_SUPPORT to be enabled
 * @note This is idempotent - multiple calls are safe
 */
void ESPWiFi::startCamera() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  log(WARNING, "📷 Camera WebSocket disabled (CONFIG_HTTPD_WS_SUPPORT is off)");
  ESP_LOGW(CAM_TAG, "WebSocket support not enabled in build configuration");
  return;
#else
  if (s_camWsStarted) {
    ESP_LOGD(CAM_TAG, "Camera WebSocket already started");
    return;
  }

  ESP_LOGI(CAM_TAG, "Starting camera WebSocket endpoint at /camera");

  // Start WebSocket endpoint
  // - Path: /camera
  // - Requires authentication via ?token= parameter
  // - No onMessage handler (server -> client only)
  // - Max broadcast length: 128KB (for high-quality JPEG frames)
  s_camWsStarted =
      s_camWs.begin("/camera", // URI path
                    this,      // Context pointer (ESPWiFi instance)
                    nullptr,   // onMessage callback (not needed for camera)
                    nullptr,   // onConnect callback
                    nullptr,   // onDisconnect callback
                    32, // Max message length for incoming (small, only control)
                    128 * 1024, // Max broadcast length (128KB for JPEG frames)
                    true        // Require authentication
      );

  if (!s_camWsStarted) {
    log(ERROR, "📷 Camera WebSocket failed to start");
    ESP_LOGE(CAM_TAG, "Failed to initialize WebSocket endpoint");
  } else {
    log(INFO, "📷 Camera WebSocket started");
    ESP_LOGI(CAM_TAG, "WebSocket endpoint ready for connections");
  }
#endif
}

/**
 * @brief Deinitialize camera hardware and driver
 *
 * Cleanly shuts down the camera subsystem:
 * 1. Closes all active WebSocket connections
 * 2. Deinitializes the camera driver
 * 3. Resets internal state flags
 *
 * This is safe to call even if camera is not initialized (no-op).
 * Mutex-protected to prevent concurrent access during shutdown.
 */
void ESPWiFi::deinitCamera() {
  ESP_LOGI(CAM_TAG, "Deinitializing camera");

  // Stop streaming to clients first (best-effort)
  if (s_camWsStarted) {
    ESP_LOGD(CAM_TAG, "Closing all WebSocket connections");
    s_camWs.closeAll();
    s_camWsStarted = false;
  }

  // Acquire mutex for shutdown
  if (!takeCamMutex_(pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_INIT_MS))) {
    log(WARNING, "📷 Camera mutex timeout during deinit");
    ESP_LOGW(CAM_TAG, "Failed to acquire mutex for deinit, proceeding anyway");
    // Continue anyway - we need to clean up
  }

  if (s_cameraInitialized) {
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) {
      ESP_LOGW(CAM_TAG, "esp_camera_deinit() returned: %s",
               esp_err_to_name(err));
    }
    s_cameraInitialized = false;
    log(INFO, "📷 Camera deinitialized");
    ESP_LOGI(CAM_TAG, "Camera hardware deinitialized");
  }

  giveCamMutex_();
}

/**
 * @brief Clear camera frame buffer
 *
 * Drains a few frames from the buffer to ensure fresh data after
 * reconfiguration. This is particularly useful after changing sensor
 * settings (brightness, exposure, etc.) to avoid showing stale frames.
 *
 * @note This is best-effort and watchdog-safe
 */
void ESPWiFi::clearCameraBuffer() {
  ESP_LOGD(CAM_TAG, "Clearing camera frame buffer");

  // Drain up to 2 frames to get latest data
  for (int i = 0; i < 2; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      ESP_LOGV(CAM_TAG, "Cleared frame %d from buffer", i + 1);
    } else {
      ESP_LOGV(CAM_TAG, "Buffer empty after %d frames", i);
      break;
    }
    // Feed watchdog to prevent timeout
    feedWatchDog();
  }
}

// ============================================================================
// Camera Configuration
// ============================================================================

/**
 * @brief Update camera sensor settings from configuration
 *
 * Applies user-configurable sensor parameters:
 * - brightness: -2 to 2
 * - contrast: -2 to 2
 * - saturation: -2 to 2
 * - exposure_level: -2 to 2 (AE level)
 * - exposure_value: 0 to 1200 (AEC value)
 * - agc_gain: 0 to 30 (AGC gain)
 * - gain_ceiling: 0 to 6 (gain ceiling)
 * - white_balance: 0 or 1 (enable/disable)
 * - awb_gain: 0 or 1 (enable/disable AWB gain)
 * - wb_mode: 0 to 4 (white balance mode)
 *
 * All parameters are bounds-checked to prevent invalid values from
 * crashing the sensor driver.
 *
 * @note Safe to call even if camera is not initialized (no-op)
 */
void ESPWiFi::updateCameraSettings() {
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    ESP_LOGW(CAM_TAG, "Cannot update settings: sensor not available");
    return;
  }

  ESP_LOGI(CAM_TAG, "Updating camera sensor settings from configuration");

  // Helper lambda: get integer config value with bounds checking
  auto getInt = [&](const char *key, int def, int minv, int maxv) -> int {
    if (config["camera"][key].isNull()) {
      return def;
    }
    int v = config["camera"][key].as<int>();
    if (v < minv) {
      ESP_LOGW(CAM_TAG, "Config %s=%d below min %d, clamping", key, v, minv);
      v = minv;
    }
    if (v > maxv) {
      ESP_LOGW(CAM_TAG, "Config %s=%d above max %d, clamping", key, v, maxv);
      v = maxv;
    }
    return v;
  };

  // Apply sensor settings (bounds-checked)
  int brightness = getInt("brightness", 1, -2, 2);
  int contrast = getInt("contrast", 1, -2, 2);
  int saturation = getInt("saturation", 1, -2, 2);
  int ae_level = getInt("exposure_level", 1, -2, 2);
  int aec_value = getInt("exposure_value", 400, 0, 1200);
  int agc_gain = getInt("agc_gain", 2, 0, 30);
  int gain_ceiling = getInt("gain_ceiling", 2, 0, 6);
  int whitebal = getInt("white_balance", 1, 0, 1);
  int awb_gain = getInt("awb_gain", 1, 0, 1);
  int wb_mode = getInt("wb_mode", 0, 0, 4);

  // Set sensor parameters
  s->set_brightness(s, brightness);
  s->set_contrast(s, contrast);
  s->set_saturation(s, saturation);
  s->set_ae_level(s, ae_level);
  s->set_aec_value(s, aec_value);
  s->set_agc_gain(s, agc_gain);
  s->set_gainceiling(s, (gainceiling_t)gain_ceiling);
  s->set_whitebal(s, whitebal);
  s->set_awb_gain(s, awb_gain);
  s->set_wb_mode(s, wb_mode);

  ESP_LOGI(CAM_TAG,
           "Camera settings applied: brightness=%d, contrast=%d, "
           "saturation=%d, ae_level=%d",
           brightness, contrast, saturation, ae_level);
  log(DEBUG, "📷 Camera settings updated");
}

/**
 * @brief Configuration change handler for camera subsystem
 *
 * Called when camera-related configuration changes. Handles:
 * - Camera enable/disable
 * - Camera installation status
 * - Setting updates
 *
 * This is the main entry point for responding to config updates from
 * the HTTP API or startup sequence.
 */
void ESPWiFi::cameraConfigHandler() {
  const bool enabled = config["camera"]["enabled"].as<bool>();
  const bool installed = config["camera"]["installed"].as<bool>();

  ESP_LOGD(CAM_TAG, "Config handler: enabled=%d, installed=%d", enabled,
           installed);

  if (!installed) {
    ESP_LOGI(CAM_TAG,
             "Camera marked as not installed, skipping initialization");
    return;
  }

  if (!enabled) {
    log(INFO, "📷 Camera disabled in configuration");
    ESP_LOGI(CAM_TAG, "Camera disabled, deinitializing");
    deinitCamera();
    return;
  }

  // Enabled and installed: initialize and start WebSocket endpoint
  log(INFO, "📷 Camera enabled in configuration");
  ESP_LOGI(CAM_TAG, "Camera enabled, initializing");

  bool initSuccess = initCamera();
  if (initSuccess) {
    startCamera();
  } else {
    ESP_LOGE(CAM_TAG, "Failed to initialize camera hardware");
  }
}

// ============================================================================
// Camera Streaming
// ============================================================================

/**
 * @brief Stream camera frames to WebSocket clients
 *
 * This function should be called periodically from the main loop. It:
 * 1. Checks if streaming is enabled and clients are connected
 * 2. Rate-limits frame capture based on configured FPS
 * 3. Captures a JPEG frame from the camera
 * 4. Broadcasts the frame to all connected WebSocket clients
 *
 * The function is designed to be watchdog-safe and non-blocking. If the
 * mutex is busy or no frame is available, it returns immediately without
 * waiting.
 *
 * @note Safe to call even if camera is disabled (no-op)
 * @note Requires CONFIG_HTTPD_WS_SUPPORT
 */
void ESPWiFi::streamCamera() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
  // Quick exit if streaming not enabled
  if (!config["camera"]["enabled"].as<bool>()) {
    return;
  }

  // Check if WebSocket is active and has clients
  if (!s_camWsStarted || s_camWs.numClients() == 0) {
    return;
  }

  // Ensure camera is initialized
  if (!s_cameraInitialized) {
    if (!initCamera()) {
      return;
    }
  }

  // Extract and validate frame rate from config
  int frameRate = config["camera"]["frameRate"].isNull()
                      ? CAM_DEFAULT_FPS
                      : config["camera"]["frameRate"].as<int>();

  // Clamp to safe range
  if (frameRate < CAM_MIN_FPS)
    frameRate = CAM_MIN_FPS;
  if (frameRate > CAM_MAX_FPS)
    frameRate = CAM_MAX_FPS;

  const int intervalMs = 1000 / frameRate;

  // Use interval timer for rate limiting
  static IntervalTimer frameTimer(1000);
  frameTimer.setIntervalMs((uint32_t)intervalMs);

  const int64_t nowUs = esp_timer_get_time();
  if (!frameTimer.shouldRunAt(nowUs)) {
    return; // Not time for next frame yet
  }

  // Try to acquire mutex with short timeout (non-blocking approach)
  if (!takeCamMutex_(pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_QUICK_MS))) {
    ESP_LOGV(CAM_TAG, "Skipping frame - mutex busy");
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  giveCamMutex_();

  // Validate frame buffer
  if (!fb) {
    ESP_LOGW(CAM_TAG, "Failed to capture frame");
    return;
  }

  if (fb->format != PIXFORMAT_JPEG || fb->buf == nullptr || fb->len == 0) {
    ESP_LOGW(CAM_TAG, "Invalid frame: format=%d, buf=%p, len=%zu", fb->format,
             fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return;
  }

  // Broadcast frame to all connected clients
  // Each frame is sent as a single binary WebSocket message
  ESP_LOGV(CAM_TAG, "Broadcasting frame: %zu bytes to %d clients", fb->len,
           s_camWs.numClients());

  esp_err_t err = s_camWs.binaryAll((const uint8_t *)fb->buf, fb->len);
  if (err != ESP_OK) {
    ESP_LOGW(CAM_TAG, "Frame broadcast failed: %s", esp_err_to_name(err));
  }

  esp_camera_fb_return(fb);
#endif
}

// ============================================================================
// HTTP Response Helpers
// ============================================================================

/**
 * @brief Send a single camera snapshot via HTTP
 *
 * Captures a single JPEG frame and sends it as an HTTP response with
 * appropriate headers (Content-Type: image/jpeg). The response is
 * chunked to prevent watchdog timeouts on large frames.
 *
 * @param req HTTP request handle
 * @param clientInfo Client information string for logging
 * @return ESP_OK on success, error code otherwise
 *
 * @note Returns 503 if camera is not available or busy
 * @note Returns 500 if frame capture fails
 * @note Chunks response to stay watchdog-safe [[memory:12698303]]
 */
esp_err_t ESPWiFi::sendCameraSnapshot(httpd_req_t *req,
                                      const std::string &clientInfo) {
  if (req == nullptr) {
    ESP_LOGE(CAM_TAG, "sendCameraSnapshot called with null request");
    return ESP_ERR_INVALID_ARG;
  }

  // Check camera availability
  if (!s_cameraInitialized) {
    ESP_LOGW(CAM_TAG, "Snapshot request but camera not initialized");
    return sendJsonResponse(req, 503, "{\"error\":\"Camera not available\"}",
                            &clientInfo);
  }

  // Try to acquire mutex (don't block request handler too long)
  if (!takeCamMutex_(pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_INIT_MS))) {
    ESP_LOGW(CAM_TAG, "Snapshot request but camera busy");
    return sendJsonResponse(req, 503, "{\"error\":\"Camera busy\"}",
                            &clientInfo);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  giveCamMutex_();

  // Validate frame capture
  if (!fb) {
    ESP_LOGE(CAM_TAG, "Frame capture failed for snapshot");
    return sendJsonResponse(req, 500, "{\"error\":\"Capture failed\"}",
                            &clientInfo);
  }

  if (fb->format != PIXFORMAT_JPEG || fb->buf == nullptr || fb->len == 0) {
    ESP_LOGE(CAM_TAG, "Invalid frame captured: format=%d, buf=%p, len=%zu",
             fb->format, fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return sendJsonResponse(req, 500, "{\"error\":\"Invalid frame\"}",
                            &clientInfo);
  }

  ESP_LOGI(CAM_TAG, "Sending snapshot: %zu bytes", fb->len);

  // Set response headers
  httpd_resp_set_type(req, "image/jpeg");
  // Prevent browser caching of old frames
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  // Chunk send to keep watchdog-safe (8KB chunks)
  const size_t CHUNK = 8192;
  size_t sent = 0;
  esp_err_t ret = ESP_OK;

  while (sent < fb->len) {
    size_t toSend = (fb->len - sent > CHUNK) ? CHUNK : (fb->len - sent);
    ret = httpd_resp_send_chunk(req, (const char *)fb->buf + sent, toSend);
    if (ret != ESP_OK) {
      ESP_LOGE(CAM_TAG, "Chunk send failed at byte %zu: %s", sent,
               esp_err_to_name(ret));
      break;
    }
    sent += toSend;

    // Feed watchdog between chunks
    feedWatchDog();
  }

  // Finalize chunked transfer
  if (ret == ESP_OK) {
    ret = httpd_resp_send_chunk(req, nullptr, 0);
  } else {
    // Still send terminator on error to close connection properly
    (void)httpd_resp_send_chunk(req, nullptr, 0);
  }

  esp_camera_fb_return(fb);

  // Log access
  int statusCode = (ret == ESP_OK) ? 200 : 500;
  logAccess(statusCode, clientInfo, sent);

  if (ret == ESP_OK) {
    ESP_LOGI(CAM_TAG, "Snapshot sent successfully: %zu bytes", sent);
  } else {
    ESP_LOGE(CAM_TAG, "Snapshot send failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

#else // No camera model defined or ESPWiFi_CAMERA_ENABLED not set

// Provide stub implementations when camera is disabled
#include "ESPWiFi.h"

bool ESPWiFi::initCamera() { return false; }
void ESPWiFi::startCamera() {}
void ESPWiFi::deinitCamera() {}
void ESPWiFi::clearCameraBuffer() {}
void ESPWiFi::updateCameraSettings() {}
void ESPWiFi::cameraConfigHandler() {}
void ESPWiFi::streamCamera() {}
esp_err_t ESPWiFi::sendCameraSnapshot(httpd_req_t *req,
                                      const std::string &clientInfo) {
  (void)req;
  (void)clientInfo;
  return ESP_ERR_NOT_SUPPORTED;
}

#endif // Camera model check
