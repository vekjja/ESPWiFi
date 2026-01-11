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
 * @note Camera functionality is built when a camera model is selected
 *       (via ESPWiFi_CAMERA_MODEL_*; see `include/CameraPins.h`).
 * @note Requires ESPWiFi_CAMERA_MODEL_* to be defined (e.g.,
 *       ESPWiFi_CAMERA_MODEL_XIAO_ESP32S3)
 * @note WebSocket streaming requires CONFIG_HTTPD_WS_SUPPORT
 */

// Always include project header; it conditionally pulls in esp_camera only when
// a camera model is selected, and defines ESPWiFi_HAS_CAMERA.
#include <CloudTunnel.h>

#include "ESPWiFi.h"

// Only compile when a camera model is selected
#if ESPWiFi_HAS_CAMERA

#include <CameraPins.h>
#include <errno.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>
#include <sdkconfig.h>
#include <sys/stat.h>
#include <time.h>

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

static SemaphoreHandle_t s_camMutex = nullptr; ///< Mutex for concurrent access
static uint32_t s_lastInitAttemptMs = 0;       ///< Last init attempt timestamp
static uint32_t s_initBackoffMs = 0;           ///< Current backoff delay
static uint8_t s_consecutiveInitFailures = 0;  ///< Failed init attempts

// ============================================================================
// Camera Mutex Helpers
// ============================================================================

/**
 * @brief Acquire camera mutex with timeout
 *
 * Lazily creates the mutex on first call. Uses a reasonable timeout to
 * prevent indefinite blocking of HTTP request handlers or streaming tasks.
 *
 * @param espWifi ESPWiFi instance for logging (can be nullptr)
 * @param ticks Timeout in FreeRTOS ticks (default: 60ms)
 * @return true if mutex acquired, false on timeout or creation failure
 */
static bool
takeCamMutex_(ESPWiFi *espWifi = nullptr,
              TickType_t ticks = pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_FRAME_MS)) {
  // Lazy initialization of mutex
  if (s_camMutex == nullptr) {
    s_camMutex = xSemaphoreCreateMutex();
    if (s_camMutex == nullptr) {
      if (espWifi)
        espWifi->log(ERROR, "📷 Failed to create camera mutex");
      return false;
    }
  }

  if (xSemaphoreTake(s_camMutex, ticks) != pdTRUE) {
    if (espWifi) {
      espWifi->log(WARNING, "📷 Camera mutex timeout after %lu ms",
                   (unsigned long)(ticks * portTICK_PERIOD_MS));
    }
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
 * if already initialized. Uses exponential backoff after failures to allow
 * PSRAM to defragment and hardware to stabilize.
 *
 * @return true if camera is initialized (or was already), false on failure
 *
 * @note Does not abort on failure per ESP32 robustness best practices
 * @note Safe to call from HTTP handlers or configuration update tasks
 */
bool ESPWiFi::initCamera() {
  // Early return if already initialized
  if (camera != nullptr) {
    return true;
  }

  // Exponential backoff after failures to allow PSRAM/hardware to recover
  const uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
  if (s_initBackoffMs > 0 && (nowMs - s_lastInitAttemptMs) < s_initBackoffMs) {
    return false; // Still in backoff period
  }

  // Re-entrancy guard: prevent concurrent init attempts
  static bool initInProgress = false;
  if (initInProgress) {
    return false;
  }
  initInProgress = true;
  s_lastInitAttemptMs = nowMs;

  log(INFO, "📷 Initializing Camera");

  // Check PSRAM availability and free space before init
  bool usingPSRAM = false;
#ifdef CONFIG_SPIRAM
  usingPSRAM = esp_psram_is_initialized();
#endif
  if (usingPSRAM) {
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t totalPSRAM = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    log(DEBUG, "📷 PSRAM before init: %u / %u bytes free", freePSRAM,
        totalPSRAM);

    // Each SVGA frame buffer is ~96KB, we need at least 2 buffers
    const size_t minRequiredPSRAM = 200 * 1024; // 200KB safety margin
    if (freePSRAM < minRequiredPSRAM) {
      log(WARNING,
          "📷 Insufficient PSRAM for camera (%u bytes free; need %u). Falling "
          "back to DRAM mode.",
          freePSRAM, minRequiredPSRAM);
      usingPSRAM = false;
    }
  }

  // Acquire mutex with extended timeout for init operations
  if (!takeCamMutex_(this, pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_INIT_MS))) {
    log(ERROR, "📷 Camera mutex timeout during init");
    initInProgress = false;
    // Apply backoff
    s_consecutiveInitFailures++;
    s_initBackoffMs = 1000 << (s_consecutiveInitFailures > 4
                                   ? 4
                                   : s_consecutiveInitFailures); // Max 16s
    return false;
  }

  // Check if driver has sensor already (shouldn't happen, but be defensive)
  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {
    log(WARNING, "📷 Camera sensor already exists in driver");
    camera = s; // Cache the sensor pointer
    giveCamMutex_();
    initInProgress = false;
    s_consecutiveInitFailures = 0;
    s_initBackoffMs = 0;
    return true;
  }

  // Configure camera hardware settings
  camera_config_t cam = {};

  // LEDC peripheral for camera clock generation
  cam.ledc_channel = LEDC_CHANNEL_0;
  cam.ledc_timer = LEDC_TIMER_0;

  // Pin configuration from CameraPins.h (based on CAMERA_MODEL_* build flag)
  cam.pin_d0 = Y2_GPIO_NUM;
  cam.pin_d1 = Y3_GPIO_NUM;
  cam.pin_d2 = Y4_GPIO_NUM;
  cam.pin_d3 = Y5_GPIO_NUM;
  cam.pin_d4 = Y6_GPIO_NUM;
  cam.pin_d5 = Y7_GPIO_NUM;
  cam.pin_d6 = Y8_GPIO_NUM;
  cam.pin_d7 = Y9_GPIO_NUM;
  cam.pin_xclk = XCLK_GPIO_NUM;
  cam.pin_pclk = PCLK_GPIO_NUM;
  cam.pin_vsync = VSYNC_GPIO_NUM;
  cam.pin_href = HREF_GPIO_NUM;
  cam.pin_sccb_sda = SIOD_GPIO_NUM;
  cam.pin_sccb_scl = SIOC_GPIO_NUM;
  cam.pin_pwdn = PWDN_GPIO_NUM;
  cam.pin_reset = RESET_GPIO_NUM;

  // Clock frequency: 20MHz is conservative and widely compatible
  cam.xclk_freq_hz = 20000000;

  // Output format: JPEG for efficient wireless transmission
  cam.pixel_format = PIXFORMAT_JPEG;

  // Adaptive buffering based on PSRAM availability
  if (usingPSRAM) {
    // For cloud streaming, use smaller frames to avoid tunnel overflow
    if (cameraCloudMode_) {
      cam.frame_size = FRAMESIZE_QVGA; // 320x240 for cloud
      cam.jpeg_quality = 30;           // More compression
      cam.fb_count = 1;                // Single buffer
      log(INFO, "📷 Cloud mode: frame_size=QVGA, quality=30, buffers=1");
    } else {
      cam.frame_size = FRAMESIZE_SVGA; // 800x600 for LAN
      cam.jpeg_quality = 15;           // Better quality
      cam.fb_count = 1;                // Single buffer (prevents FB-OVF)
      log(INFO, "📷 LAN mode: frame_size=SVGA, quality=15, buffers=1");
    }
    cam.fb_location = CAMERA_FB_IN_PSRAM;
    cam.grab_mode =
        CAMERA_GRAB_LATEST; // Always get latest frame, drop old ones
  } else {
    // Without PSRAM, use smaller frames and single buffer
    cam.frame_size = FRAMESIZE_QVGA;     // 320x240
    cam.jpeg_quality = 25;               // Lower quality for size
    cam.fb_count = 1;                    // Single buffer
    cam.fb_location = CAMERA_FB_IN_DRAM; // Internal RAM only
    cam.grab_mode = CAMERA_GRAB_LATEST;

    log(INFO, "📷 No PSRAM: frame_size=QVGA, quality=25, buffers=1");
  }

  // Extract frame rate from configuration (with bounds checking)
  int frameRate = config["camera"]["frameRate"].isNull()
                      ? CAM_DEFAULT_FPS
                      : config["camera"]["frameRate"].as<int>();

  // Clamp to safe range
  if (frameRate < CAM_MIN_FPS) {
    frameRate = CAM_MIN_FPS;
  }
  if (frameRate > CAM_MAX_FPS) {
    frameRate = CAM_MAX_FPS;
  }

  // Initialize the camera driver
  // Note: We do NOT use ESP_ERROR_CHECK here to avoid aborting on failure
  esp_err_t err = esp_camera_init(&cam);
  if (err != ESP_OK) {
    const char *errName = esp_err_to_name(err);
    log(ERROR, "📷 Camera Init Failed: %s (0x%x)", errName, err);

    // Best-effort cleanup
    (void)esp_camera_deinit();

    giveCamMutex_();
    initInProgress = false;

    // Apply exponential backoff: 1s, 2s, 4s, 8s, 16s
    s_consecutiveInitFailures++;
    s_initBackoffMs =
        1000 << (s_consecutiveInitFailures > 4 ? 4 : s_consecutiveInitFailures);
    log(WARNING, "📷 Init failed %d times, backing off for %u ms",
        s_consecutiveInitFailures, s_initBackoffMs);
    return false;
  }

  // Cache the sensor pointer immediately after successful init
  camera = esp_camera_sensor_get();
  if (camera == nullptr) {
    log(ERROR, "📷 Failed to get camera sensor after init");
    (void)esp_camera_deinit();
    giveCamMutex_();
    initInProgress = false;

    s_consecutiveInitFailures++;
    s_initBackoffMs =
        1000 << (s_consecutiveInitFailures > 4 ? 4 : s_consecutiveInitFailures);
    return false;
  }

  // Apply user configuration settings (brightness, contrast, etc.)
  updateCameraSettings();

  giveCamMutex_();
  initInProgress = false;

  // Reset backoff on success
  s_consecutiveInitFailures = 0;
  s_initBackoffMs = 0;

  log(INFO, "📷 Camera initialized (%s, %d FPS)", usingPSRAM ? "PSRAM" : "DRAM",
      frameRate);

  return true;
}

/**
 * @brief Deinitialize camera hardware and driver
 *
 * Cleanly shuts down the camera subsystem:
 * 1. Clears frame buffers to prevent stale data
 * 2. Deinitializes the camera driver
 * 3. Resets internal state flags
 * 4. Allows hardware and PSRAM to settle before reuse
 *
 * This is safe to call even if camera is not initialized (no-op).
 * Mutex-protected to prevent concurrent access during shutdown.
 */
void ESPWiFi::deinitCamera() {
  // Early exit if already deinitialized
  if (camera == nullptr) {
    return;
  }

  // Acquire mutex for shutdown to prevent concurrent deinit
  if (!takeCamMutex_(this, pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_INIT_MS))) {
    // If we can't get mutex, another thread is likely already deiniting
    return;
  }

  // Double-check after acquiring mutex
  if (camera == nullptr) {
    giveCamMutex_();
    return;
  }

  log(INFO, "📷 Deinitializing camera");

  // Clear any pending frames from buffer before deinit
  log(DEBUG, "📷 Clearing frame buffers before deinit");
  for (int i = 0; i < 3; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
    } else {
      break;
    }
    feedWatchDog(1);
  }

  // Deinitialize the camera driver
  esp_err_t err = esp_camera_deinit();
  if (err != ESP_OK) {
    log(WARNING, "📷 esp_camera_deinit() returned: %s", esp_err_to_name(err));
  }

  // Clear the cached sensor pointer
  camera = nullptr;

  giveCamMutex_();

  // CRITICAL: Allow PSRAM to defragment and hardware to stabilize
  // Also force garbage collection to recover memory
  feedWatchDog(100);

#ifdef CONFIG_SPIRAM
  // Force PSRAM compaction by allocating and freeing a small chunk
  if (esp_psram_is_initialized()) {
    void *temp = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (temp) {
      heap_caps_free(temp);
    }
  }
#endif

  // Log memory status after deinit
  bool psramAvailable = false;
#ifdef CONFIG_SPIRAM
  psramAvailable = esp_psram_is_initialized();
#endif
  if (psramAvailable) {
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t totalPSRAM = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    log(DEBUG, "📷 PSRAM after deinit: %u / %u bytes free", freePSRAM,
        totalPSRAM);
  }

  log(INFO, "📷 Camera deinitialized");
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
  log(DEBUG, "📷 Clearing camera frame buffer");

  // Drain up to 2 frames to get latest data
  for (int i = 0; i < 2; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      log(VERBOSE, "📷 Cleared frame %d from buffer", i + 1);
    } else {
      log(VERBOSE, "📷 Buffer empty after %d frames", i);
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
 * @brief Print camera settings as indented JSON in debug log
 *
 * Retrieves all current camera sensor settings and formats them as
 * indented JSON for debugging and diagnostics. Safe to call even if
 * camera is not initialized (no-op).
 *
 * @note Only logs if sensor is available
 */
void ESPWiFi::printCameraSettings() {
  if (camera == nullptr) {
    log(WARNING, "📷 Cannot print settings: sensor not available");
    return;
  }

  // Helper to get config value with default
  auto getInt = [&](const char *key, int def) -> int {
    return config["camera"][key].isNull() ? def
                                          : config["camera"][key].as<int>();
  };

  // Retrieve all settings from config
  int brightness = getInt("brightness", 1);
  int contrast = getInt("contrast", 1);
  int saturation = getInt("saturation", 1);
  int sharpness = getInt("sharpness", 0);
  int denoise = getInt("denoise", 0);
  int quality = getInt("quality", 12);
  int ae_level = getInt("exposure_level", 1);
  int aec_value = getInt("exposure_value", 400);
  int agc_gain = getInt("agc_gain", 2);
  int gain_ceiling = getInt("gain_ceiling", 2);
  int whitebal = getInt("white_balance", 1);
  int awb_gain = getInt("awb_gain", 1);
  int wb_mode = getInt("wb_mode", 0);
  int rotation = getInt("rotation", 0);
  int frameRate = getInt("frameRate", CAM_DEFAULT_FPS);

  // Build JSON string with proper indentation
  log(DEBUG,
      "📷 Camera settings JSON:\n"
      "{\n"
      "  \"brightness\": %d,\n"
      "  \"contrast\": %d,\n"
      "  \"saturation\": %d,\n"
      "  \"sharpness\": %d,\n"
      "  \"denoise\": %d,\n"
      "  \"quality\": %d,\n"
      "  \"exposure_level\": %d,\n"
      "  \"exposure_value\": %d,\n"
      "  \"agc_gain\": %d,\n"
      "  \"gain_ceiling\": %d,\n"
      "  \"white_balance\": %d,\n"
      "  \"awb_gain\": %d,\n"
      "  \"wb_mode\": %d,\n"
      "  \"rotation\": %d,\n"
      "  \"frameRate\": %d\n"
      "}",
      brightness, contrast, saturation, sharpness, denoise, quality, ae_level,
      aec_value, agc_gain, gain_ceiling, whitebal, awb_gain, wb_mode, rotation,
      frameRate);
}

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
  if (camera == nullptr) {
    log(WARNING, "📷 Cannot update settings: sensor not available");
    return;
  }

  log(INFO, "📷 Updating camera sensor settings from configuration");

  // Helper lambda: get integer config value with bounds checking
  auto getInt = [&](const char *key, int def, int minv, int maxv) -> int {
    if (config["camera"][key].isNull()) {
      return def;
    }
    int v = config["camera"][key].as<int>();
    if (v < minv) {
      log(WARNING, "📷 Config %s=%d below min %d, clamping", key, v, minv);
      v = minv;
    }
    if (v > maxv) {
      log(WARNING, "📷 Config %s=%d above max %d, clamping", key, v, maxv);
      v = maxv;
    }
    return v;
  };

  // Apply sensor settings (bounds-checked)
  int brightness = getInt("brightness", 1, -2, 2);
  int contrast = getInt("contrast", 1, -2, 2);
  int saturation = getInt("saturation", 1, -2, 2);
  int sharpness = getInt("sharpness", 0, -2, 2);
  int denoise = getInt("denoise", 0, 0, 8);
  int quality = getInt("quality", 12, 0, 63); // Lower = better quality
  int ae_level = getInt("exposure_level", 1, -2, 2);
  int aec_value = getInt("exposure_value", 400, 0, 1200);
  int agc_gain = getInt("agc_gain", 2, 0, 30);
  int gain_ceiling = getInt("gain_ceiling", 2, 0, 6);
  int whitebal = getInt("white_balance", 1, 0, 1);
  int awb_gain = getInt("awb_gain", 1, 0, 1);
  int wb_mode = getInt("wb_mode", 0, 0, 4);
  int rotation = getInt("rotation", 0, 0, 270);

  // Cloud mode profile: ensure sensor-level settings don't undo our
  // low-bandwidth init profile (initCamera sets QVGA/quality=30, but sensor
  // setters can override it back to config values).
  if (cameraCloudMode_) {
    // Force smaller frames over cloud tunnel.
    if (camera->set_framesize) {
      camera->set_framesize(camera, FRAMESIZE_QVGA);
    }
    // Increase compression (larger number = lower quality / smaller frames).
    if (quality < 30) {
      quality = 30;
    }
  }

  // Set sensor parameters
  camera->set_brightness(camera, brightness);
  camera->set_contrast(camera, contrast);
  camera->set_saturation(camera, saturation);
  if (camera->set_sharpness)
    camera->set_sharpness(camera, sharpness);
  if (camera->set_denoise)
    camera->set_denoise(camera, denoise);
  if (camera->set_quality)
    camera->set_quality(camera, quality);
  camera->set_ae_level(camera, ae_level);
  camera->set_aec_value(camera, aec_value > 0 ? aec_value : 1);
  camera->set_agc_gain(camera, agc_gain);
  camera->set_gainceiling(camera, (gainceiling_t)gain_ceiling);
  camera->set_whitebal(camera, whitebal);
  camera->set_awb_gain(camera, awb_gain);
  camera->set_wb_mode(camera, wb_mode);

  // Apply rotation using vflip and hflip
  // Note: Rotation is approximate using sensor flip functions
  // 0°   = no flips
  // 90°  = hflip only
  // 180° = vflip + hflip
  // 270° = vflip only
  switch (rotation) {
  case 0:
    camera->set_vflip(camera, 0);
    camera->set_hmirror(camera, 0);
    break;
  case 90:
    camera->set_vflip(camera, 0);
    camera->set_hmirror(camera, 1);
    break;
  case 180:
    camera->set_vflip(camera, 1);
    camera->set_hmirror(camera, 1);
    break;
  case 270:
    camera->set_vflip(camera, 1);
    camera->set_hmirror(camera, 0);
    break;
  default:
    log(WARNING, "📷 Invalid rotation value: %d, using 0°", rotation);
    camera->set_vflip(camera, 0);
    camera->set_hmirror(camera, 0);
    break;
  }

  log(INFO, "📷 Camera settings applied");
  printCameraSettings();
}

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
  if (!ctrlSocStarted) {
    return;
  }

  // Check for subscribers
  const size_t lanSubs = (size_t)cameraStreamSubCount_;
  const bool cloudSub = cameraStreamCloudSubscribed_ && cloudUIConnected();
  const bool snapPending = (cameraSnapshotFd_ != 0);
  const bool hasConsumers = (lanSubs > 0 || cloudSub || snapPending);

  // No consumers? Deinit camera after grace period
  static bool noConsumerPrevious = false;
  static uint32_t noConsumerStartMs = 0;
  if (!hasConsumers) {
    if (camera != nullptr) {
      if (!noConsumerPrevious) {
        // First time seeing no consumers - start grace period
        noConsumerStartMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
        noConsumerPrevious = true;
        return;
      }
      // Check if grace period expired (2 seconds)
      uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
      if ((nowMs - noConsumerStartMs) > 2000) {
        deinitCamera();
        noConsumerPrevious = false;
      }
    } else {
      noConsumerPrevious = false;
    }
    return;
  }
  noConsumerPrevious = false;

  // Initialize camera if needed
  if (camera == nullptr) {
    cameraCloudMode_ = (cloudSub && lanSubs == 0);
    if (!initCamera()) {
      return;
    }
  }

  // Get frame rate
  int frameRate = config["camera"]["frameRate"].isNull()
                      ? CAM_DEFAULT_FPS
                      : config["camera"]["frameRate"].as<int>();
  if (frameRate < CAM_MIN_FPS)
    frameRate = CAM_MIN_FPS;
  if (frameRate > CAM_MAX_FPS)
    frameRate = CAM_MAX_FPS;

  // Cloud-only FPS cap
  if (cloudSub && lanSubs == 0) {
    int cloudFps = config["cloudTunnel"]["maxFps"].isNull()
                       ? 3
                       : config["cloudTunnel"]["maxFps"].as<int>();
    if (cloudFps < 1)
      cloudFps = 3;
    if (cloudFps > CAM_MAX_FPS)
      cloudFps = CAM_MAX_FPS;
    if (frameRate > cloudFps)
      frameRate = cloudFps;
  }

  // Rate limit
  static IntervalTimer frameTimer(1000);
  frameTimer.setIntervalMs((uint32_t)(1000 / frameRate));
  if (!frameTimer.shouldRunAt(esp_timer_get_time())) {
    return;
  }

  // Capture frame
  if (!takeCamMutex_(this, pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_QUICK_MS))) {
    return;
  }

  // Drain any old frames first to prevent FB-OVF
  // This ensures we always get the latest frame
  camera_fb_t *fb = nullptr;
  int drained = 0;
  while (drained < 5) { // Max 5 frames to prevent infinite loop
    fb = esp_camera_fb_get();
    if (!fb) {
      break;
    }
    drained++;
    // If this is not the last available frame, return it and get next
    camera_fb_t *next = esp_camera_fb_get();
    if (next) {
      esp_camera_fb_return(fb);
      fb = next;
    } else {
      // This is the last frame, use it
      break;
    }
  }

  giveCamMutex_();

  if (!fb || fb->format != PIXFORMAT_JPEG || !fb->buf || fb->len == 0) {
    if (fb) {
      esp_camera_fb_return(fb);
    }
    return;
  }

  if (drained > 1) {
    log(DEBUG, "📷 Drained %d old frames (using latest)", drained - 1);
  }

  // Send to LAN subscribers on /ws/camera
  bool lanSendSuccess = true;
  if (cameraSocStarted && lanSubs > 0) {
    static uint32_t lanFrameCount = 0;
    static uint32_t lanDropCount = 0;

    for (size_t i = 0; i < lanSubs; i++) {
      int fd = cameraStreamSubFds_[i];
      if (fd > 0) {
        esp_err_t err =
            cameraSoc.sendBinary(fd, (const uint8_t *)fb->buf, fb->len);
        if (err != ESP_OK) {
          lanSendSuccess = false;
          lanDropCount++;
          if (lanDropCount % 10 == 0) {
            log(DEBUG, "📷 LAN: dropped %lu frames (client slow)",
                lanDropCount);
          }
        }
      }
    }

    if (lanSendSuccess) {
      lanFrameCount++;
      if (lanDropCount > 0) {
        lanDropCount = 0; // Reset on successful send
      }

      // Log every 60 frames (every ~6 seconds at 10 FPS)
      if (lanFrameCount % 60 == 0) {
        log(DEBUG, "📷 LAN: sent %lu frames to %zu client(s)", lanFrameCount,
            lanSubs);
      }
    }
  }

  // Send to cloud via /ws/control tunnel (with aggressive rate limiting)
  if (cloudSub) {
    static uint32_t lastCloudSendMs = 0;
    static uint32_t cloudDropCount = 0;
    const uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
    const uint32_t cloudMinIntervalMs = 250; // Max 4 FPS for cloud

    // Only send if enough time has passed since last successful send
    if ((nowMs - lastCloudSendMs) >= cloudMinIntervalMs) {
      bool sent = sendToCloudTunnel((const uint8_t *)fb->buf, fb->len);
      if (sent) {
        lastCloudSendMs = nowMs;
        if (cloudDropCount > 0) {
          log(DEBUG, "📷 Cloud: sent frame after %lu drops", cloudDropCount);
          cloudDropCount = 0;
        }
      } else {
        cloudDropCount++;
        if (cloudDropCount % 10 == 0) {
          log(DEBUG, "📷 Cloud: dropped %lu frames (tunnel busy)",
              cloudDropCount);
        }
      }
    }
  }

  // Snapshot
  if (snapPending) {
    int snapFd = cameraSnapshotFd_;
    cameraSnapshotFd_ = 0;
    if (snapFd == CloudTunnel::kCloudClientFd) {
      sendToCloudTunnel((const uint8_t *)fb->buf, fb->len);
    } else if (snapFd > 0 && cameraSocStarted) {
      cameraSoc.sendBinary(snapFd, (const uint8_t *)fb->buf, fb->len);
    }
  }

  esp_camera_fb_return(fb);
#endif
}

#ifdef CONFIG_HTTPD_WS_SUPPORT
void ESPWiFi::setCameraStreamSubscribed(int clientFd, bool enable) {
  if (clientFd == CloudTunnel::kCloudClientFd) {
    cameraStreamCloudSubscribed_ = enable;

    // When cloud streaming is disabled and no LAN subs exist, deinit camera
    if (!enable && (size_t)cameraStreamSubCount_ == 0 &&
        cameraSnapshotFd_ == 0) {
      if (camera != nullptr) {
        log(INFO, "📷 No subscribers; deinitializing camera");
        deinitCamera();
      }
      // Reset cloud mode flag
      cameraCloudMode_ = false;
    }
    return;
  }

  if (clientFd <= 0) {
    return;
  }

  // Remove if disabling
  if (!enable) {
    for (size_t i = 0; i < (size_t)cameraStreamSubCount_;) {
      if (cameraStreamSubFds_[i] == clientFd) {
        // swap-remove
        const size_t last = (size_t)cameraStreamSubCount_ - 1;
        cameraStreamSubFds_[i] = cameraStreamSubFds_[last];
        cameraStreamSubFds_[last] = 0;
        cameraStreamSubCount_ = (size_t)cameraStreamSubCount_ - 1;
        continue;
      }
      i++;
    }

    // If last consumer unsubscribed, deinit camera
    if ((size_t)cameraStreamSubCount_ == 0 && !cameraStreamCloudSubscribed_ &&
        cameraSnapshotFd_ == 0) {
      if (camera != nullptr) {
        log(INFO, "📷 No subscribers; deinitializing camera");
        deinitCamera();
      }
      // Reset cloud mode flag
      cameraCloudMode_ = false;
    }
    return;
  }

  // Add if not present
  for (size_t i = 0; i < (size_t)cameraStreamSubCount_; i++) {
    if (cameraStreamSubFds_[i] == clientFd) {
      return; // Already subscribed
    }
  }

  if ((size_t)cameraStreamSubCount_ >= kMaxCameraStreamSubscribers) {
    log(WARNING, "📷 Camera stream subscriber limit reached; ignoring fd=%d",
        clientFd);
    return;
  }

  const size_t idx = (size_t)cameraStreamSubCount_;
  cameraStreamSubFds_[idx] = clientFd;
  cameraStreamSubCount_ = idx + 1;

  // LAN subscriber means we should NOT use cloud profile
  // Note: We don't reinit here - let the camera init happen naturally
  // on the next frame capture with the correct profile
  if (cameraCloudMode_) {
    cameraCloudMode_ = false;
    if (camera != nullptr) {
      log(INFO,
          "📷 LAN subscriber added; will use normal profile on next init");
      // Deinit so next frame capture will reinit with correct profile
      deinitCamera();
    }
  }
}

void ESPWiFi::clearCameraStreamSubscribed(int clientFd) {
  // Check if already unsubscribed to prevent double-cleanup
  bool found = false;

  if (clientFd == CloudTunnel::kCloudClientFd) {
    found = cameraStreamCloudSubscribed_;
  } else if (clientFd > 0) {
    for (size_t i = 0; i < (size_t)cameraStreamSubCount_; i++) {
      if (cameraStreamSubFds_[i] == clientFd) {
        found = true;
        break;
      }
    }
  }

  if (!found) {
    return; // Already cleaned up
  }

  setCameraStreamSubscribed(clientFd, false);
}

void ESPWiFi::requestCameraSnapshot(int clientFd) {
  cameraSnapshotFd_ = clientFd;
}
#endif

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
    log(ERROR, "📷 sendCameraSnapshot called with null request");
    return ESP_ERR_INVALID_ARG;
  }

  // Check camera availability
  if (camera == nullptr) {
    log(WARNING, "📷 Snapshot request but camera not initialized");
    return sendJsonResponse(req, 503, "{\"error\":\"Camera not available\"}",
                            &clientInfo);
  }

  // Try to acquire mutex (don't block request handler too long)
  if (!takeCamMutex_(this, pdMS_TO_TICKS(CAM_MUTEX_TIMEOUT_INIT_MS))) {
    log(WARNING, "📷 Snapshot request but camera busy");
    return sendJsonResponse(req, 503, "{\"error\":\"Camera busy\"}",
                            &clientInfo);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  giveCamMutex_();

  // Validate frame capture
  if (!fb) {
    log(ERROR, "📷 Frame capture failed for snapshot");
    return sendJsonResponse(req, 500, "{\"error\":\"Capture failed\"}",
                            &clientInfo);
  }

  if (fb->format != PIXFORMAT_JPEG || fb->buf == nullptr || fb->len == 0) {
    log(ERROR, "📷 Invalid frame captured: format=%d, buf=%p, len=%zu",
        fb->format, fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return sendJsonResponse(req, 500, "{\"error\":\"Invalid frame\"}",
                            &clientInfo);
  }

  log(INFO, "📷 Sending snapshot: %zu bytes", fb->len);

  // Check if we should save to SD card
  char query[128];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    char saveParam[8];
    if (httpd_query_key_value(query, "save", saveParam, sizeof(saveParam)) ==
        ESP_OK) {
      if (strcmp(saveParam, "true") == 0 || strcmp(saveParam, "1") == 0) {
        // Save snapshot to SD card if available
        if (checkSDCard()) {
          // Create /snapshots directory if it doesn't exist
          char snapDir[128];
          snprintf(snapDir, sizeof(snapDir), "%s/snapshots",
                   sdMountPoint.c_str());

          int mkdirResult = mkdir(snapDir, 0755);
          if (mkdirResult != 0 && errno != EEXIST) {
            log(ERROR, "📷 Failed to create snapshots directory: %s (errno=%d)",
                snapDir, errno);
          } else if (mkdirResult == 0) {
            log(INFO, "📷 Created snapshots directory: %s", snapDir);
          }

          // Generate filename using milliseconds since boot
          unsigned long timestamp = millis();
          char filename[128];
          snprintf(filename, sizeof(filename), "%s/snapshots/%lu.jpg",
                   sdMountPoint.c_str(), timestamp);

          // Write snapshot to file
          FILE *file = fopen(filename, "wb");
          if (file) {
            size_t written = fwrite(fb->buf, 1, fb->len, file);
            fclose(file);
            if (written == fb->len) {
              log(INFO, "📸 Snapshot saved to SD: %s (%zu bytes)", filename,
                  fb->len);
            } else {
              log(ERROR, "📸 Failed to Write Snapshot: %zu/%zu bytes", written,
                  fb->len);
            }
          } else {
            log(ERROR, "📸 Failed to Open File for Snapshot: %s (errno=%d)",
                filename, errno);
          }
        } else {
          log(WARNING, "📸 Snapshot save requested but SD card not available");
        }
      }
    }
  }

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
      log(ERROR, "📷 Chunk send failed at byte %zu: %s", sent,
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
    log(INFO, "📷 Snapshot sent successfully: %zu bytes", sent);
  } else {
    log(ERROR, "📷 Snapshot send failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

#else // No camera model selected

// Provide stub implementations when camera is disabled
bool ESPWiFi::initCamera() { return false; }
void ESPWiFi::deinitCamera() {}
void ESPWiFi::clearCameraBuffer() {}
void ESPWiFi::updateCameraSettings() {}
void ESPWiFi::streamCamera() {}
esp_err_t ESPWiFi::sendCameraSnapshot(httpd_req_t *req,
                                      const std::string &clientInfo) {
  (void)req;
  (void)clientInfo;
  return ESP_ERR_NOT_SUPPORTED;
}

#endif // ESPWiFi_HAS_CAMERA

/**
 * @brief Configuration change handler for camera subsystem
 *
 * Called when camera-related configuration changes. Handles:
 * - Camera enable/disable
 * - Camera installation status
 * - Setting updates
 *
 * Compares configUpdate with current config to determine what changed.
 * This is called before config is updated with configUpdate values.
 *
 * This is the main entry point for responding to config updates from
 * the HTTP API or startup sequence.
 */
void ESPWiFi::cameraConfigHandler() {
#if ESPWiFi_HAS_CAMERA
  if (!config["camera"]["installed"].as<bool>()) {
    return;
  }

  // NOTE: handleConfigUpdate() calls this handler before it swaps
  // `config = configUpdate`. For any immediate hardware actions (init/reinit),
  // we must apply settings using the *new* config values in configUpdate.
  // Keep a snapshot of the old config for comparisons, but temporarily swap
  // config to configUpdate when applying.
  JsonDocument oldCfg = config;
  JsonVariantConst oldCam = oldCfg["camera"];
  JsonVariantConst newCam = configUpdate["camera"];

  bool oldEnabled = oldCam["enabled"].as<bool>();
  bool newEnabled =
      newCam["enabled"].isNull() ? oldEnabled : newCam["enabled"].as<bool>();

  // Check if any settings changed (only if they're present in configUpdate)
  bool settingsChanged = false;
  bool needsReinit = false;

  // List of camera settings that trigger updateCameraSettings()
  const char *settingKeys[] = {"frameRate",      "rotation",   "brightness",
                               "contrast",       "saturation", "sharpness",
                               "denoise",        "quality",    "exposure_level",
                               "exposure_value", "agc_gain",   "gain_ceiling",
                               "white_balance",  "awb_gain",   "wb_mode"};

  for (const char *key : settingKeys) {
    if (!newCam[key].isNull()) {
      // This setting is being updated, check if value actually changed
      auto oldVal = oldCam[key];
      auto newVal = newCam[key];
      if (oldVal != newVal) {
        settingsChanged = true;

        // Some settings are flaky/driver-dependent; for stability, we reinit
        // the camera when these change. (Requested: rotation + exposure).
        if (strcmp(key, "rotation") == 0 ||
            strcmp(key, "exposure_level") == 0 ||
            strcmp(key, "exposure_value") == 0) {
          needsReinit = true;
        }
        break;
      }
    }
  }

  // Handle enable/disable transitions
  if (newEnabled != oldEnabled) {
    if (!newEnabled) {
      deinitCamera();
    } else {
      // Enable camera: apply new config and initialize
      config = configUpdate;
      initCamera();
      if (camera != nullptr) {
        updateCameraSettings();
        clearCameraBuffer();
      }
      config = oldCfg;
    }
  } else if (newEnabled && settingsChanged) {
    // Camera is enabled and settings changed
    config = configUpdate;

    if (needsReinit) {
      log(INFO, "📷 Camera settings changed (requires reinit)");
      // Only reinit if camera is currently initialized
      if (camera != nullptr) {
        deinitCamera();
      }
      // Reinit will happen on next frame capture
    } else {
      // Settings can be applied without reinit
      log(INFO, "📷 Camera settings changed, applying updates");
      if (camera != nullptr) {
        updateCameraSettings();
        feedWatchDog(50); // Wait for sensor to apply settings
        clearCameraBuffer();
      }
    }
    config = oldCfg;
  }
#endif
}