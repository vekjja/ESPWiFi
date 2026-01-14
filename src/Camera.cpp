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
// Note: Higher frame rates (>30) may cause watchdog timeouts on some boards
#define CAM_MIN_FPS 1
#define CAM_MAX_FPS 30
#define CAM_DEFAULT_FPS 10

// ============================================================================
// Camera Runtime State
// ============================================================================

static uint32_t s_lastInitAttemptMs = 0;      ///< Last init attempt timestamp
static uint32_t s_initBackoffMs = 0;          ///< Current backoff delay
static uint8_t s_consecutiveInitFailures = 0; ///< Failed init attempts

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

  // Check if driver has sensor already (shouldn't happen, but be defensive)
  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {
    log(WARNING, "📷 Camera sensor already exists in driver");
    camera = s; // Cache the sensor pointer
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

  // Clock frequency: 10MHz for stability (20MHz can cause timeouts on some
  // sensors)
  cam.xclk_freq_hz = 10000000;

  // Output format: JPEG for efficient wireless transmission
  cam.pixel_format = PIXFORMAT_JPEG;

  // Determine orientation preference (landscape vs portrait)
  const char *orientationStr =
      config["camera"]["orientation"] | "landscape"; // Default to landscape
  bool isPortrait = (strcmp(orientationStr, "portrait") == 0);

  // Extract quality from configuration (lower = better quality, 0-63 range)
  int quality = config["camera"]["quality"].isNull()
                    ? 12
                    : config["camera"]["quality"].as<int>();
  // Clamp quality to valid range
  if (quality < 0) {
    quality = 0;
  }
  if (quality > 63) {
    quality = 63;
  }

  // Adaptive buffering based on PSRAM availability and orientation
  if (usingPSRAM) {
    // LAN streaming with SVGA resolution (or VGA for portrait)
    if (isPortrait) {
      cam.frame_size = FRAMESIZE_VGA; // 640x480 for portrait
      log(INFO, "📷 frame_size=VGA (portrait), quality=%d, buffers=1", quality);
    } else {
      cam.frame_size = FRAMESIZE_SVGA; // 800x600 for landscape
      log(INFO, "📷 frame_size=SVGA (landscape), quality=%d, buffers=1",
          quality);
    }
    cam.jpeg_quality = quality;
    cam.fb_count = 1; // Single buffer (prevents FB-OVF)
    cam.fb_location = CAMERA_FB_IN_PSRAM;
    cam.grab_mode =
        CAMERA_GRAB_LATEST; // Always get latest frame, drop old ones
  } else {
    // Without PSRAM, use smaller frames and single buffer
    if (isPortrait) {
      cam.frame_size = FRAMESIZE_QVGA; // 320x240 for portrait
      log(INFO,
          "📷 No PSRAM: frame_size=QVGA (portrait), quality=%d, buffers=1",
          quality);
    } else {
      cam.frame_size = FRAMESIZE_QVGA; // 320x240 for landscape
      log(INFO,
          "📷 No PSRAM: frame_size=QVGA (landscape), quality=%d, buffers=1",
          quality);
    }
    cam.jpeg_quality = quality;
    cam.fb_count = 1;                    // Single buffer
    cam.fb_location = CAMERA_FB_IN_DRAM; // Internal RAM only
    cam.grab_mode = CAMERA_GRAB_LATEST;
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
    initInProgress = false;

    s_consecutiveInitFailures++;
    s_initBackoffMs =
        1000 << (s_consecutiveInitFailures > 4 ? 4 : s_consecutiveInitFailures);
    return false;
  }

  // Apply user configuration settings (brightness, contrast, etc.)
  updateCameraSettings();

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
 */
void ESPWiFi::deinitCamera() {
  // Early exit if already deinitialized
  if (camera == nullptr) {
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

  auto getStr = [&](const char *key, const char *def) -> const char * {
    return config["camera"][key].isNull()
               ? def
               : config["camera"][key].as<const char *>();
  };

  // Retrieve all settings from config
  const char *orientation = getStr("orientation", "landscape");
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
      "  \"orientation\": \"%s\",\n"
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
      orientation, brightness, contrast, saturation, sharpness, denoise,
      quality, ae_level, aec_value, agc_gain, gain_ceiling, whitebal, awb_gain,
      wb_mode, rotation, frameRate);
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
    // case 0:
    //   camera->set_vflip(camera, 0);
    //   camera->set_hmirror(camera, 0);
    //   break;
    // case 90:
    //   camera->set_vflip(camera, 1);
    // camera->set_hmirror(camera, 1);
    break;
  // case 180:
  //   camera->set_vflip(camera, 1);
  //   camera->set_hmirror(camera, 1);
  //   break;
  // case 270:
  //   camera->set_vflip(camera, 1);
  //   camera->set_hmirror(camera, 0);
  //   break;
  default:
    log(WARNING, "📷 No Hardware Rotation Applied: %d, using 0°", rotation);
    // camera->set_vflip(camera, 0);
    camera->set_vflip(camera, 1);
    camera->set_hmirror(camera, 0);
    break;
  }

  log(INFO, "📷 Camera settings applied");
  printCameraSettings();
}

#ifdef CONFIG_HTTPD_WS_SUPPORT
void ESPWiFi::setCameraStreamSubscribed(int clientFd, bool enable) {
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
}

void ESPWiFi::clearCameraStreamSubscribed(int clientFd) {
  // Check if already unsubscribed to prevent double-cleanup
  bool found = false;

  if (clientFd > 0) {
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
#endif

// ============================================================================
// Snapshot Helper (shared by HTTP and WebSocket)
// ============================================================================

/**
 * @brief Take a camera snapshot and optionally save to SD card
 *
 * @param save If true, save snapshot to SD card
 * @param url Output parameter for the URL where snapshot can be accessed
 * @param errorMsg Output parameter for error message if snapshot fails
 * @return true if snapshot was taken successfully, false otherwise
 */
bool ESPWiFi::takeSnapshot(bool save, std::string &url, std::string &errorMsg) {
  // Check camera availability
  if (camera == nullptr) {
    errorMsg = "Camera not available";
    log(WARNING, "📸 Snapshot: Camera not initialized");
    return false;
  }

  // Capture frame
  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb) {
    errorMsg = "Frame capture failed";
    log(ERROR, "📸 Snapshot: Frame capture failed");
    return false;
  }

  if (fb->format != PIXFORMAT_JPEG || fb->buf == nullptr || fb->len == 0) {
    esp_camera_fb_return(fb);
    errorMsg = "Invalid frame";
    log(ERROR, "📸 Snapshot: Invalid frame captured");
    return false;
  }

  log(INFO, "📸 Snapshot captured: %zu bytes", fb->len);
  bool success = false;

  if (save) {
    // Save to SD card
    if (checkSDCard()) {
      // Create snapshots directory
      char snapDir[128];
      snprintf(snapDir, sizeof(snapDir), "%s/snapshots", sdMountPoint.c_str());

      int mkdirResult = mkdir(snapDir, 0755);
      if (mkdirResult != 0 && errno != EEXIST) {
        log(ERROR, "📸 Failed to create snapshots directory: %s (errno=%d)",
            snapDir, errno);
      } else if (mkdirResult == 0) {
        log(INFO, "📸 Created snapshots directory: %s", snapDir);
      }

      // Generate filename using timestamp
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
          char urlBuf[64];
          snprintf(urlBuf, sizeof(urlBuf), "/sd/snapshots/%lu.jpg", timestamp);
          url = std::string(urlBuf);
          success = true;
          log(INFO, "📸 Snapshot saved: %s (%zu bytes)", filename, fb->len);
        } else {
          errorMsg = "Failed to write snapshot";
          log(ERROR, "📸 Failed to write snapshot: %zu/%zu bytes", written,
              fb->len);
        }
      } else {
        errorMsg = "Failed to create file";
        log(ERROR, "📸 Failed to create snapshot file: %s (errno=%d)", filename,
            errno);
      }
    } else {
      errorMsg = "SD card not available";
      log(ERROR, "📸 Snapshot save failed: SD card not available");
    }
  } else {
    // Don't save, just return the snapshot endpoint URL
    url = "/camera/snapshot";
    success = true;
  }

  esp_camera_fb_return(fb);
  return success;
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
    log(ERROR, "📷 sendCameraSnapshot called with null request");
    return ESP_ERR_INVALID_ARG;
  }

  // Check camera availability
  if (camera == nullptr) {
    log(WARNING, "📷 Snapshot request but camera not initialized");
    return sendJsonResponse(req, 503, "{\"error\":\"Camera not available\"}",
                            &clientInfo);
  }

  camera_fb_t *fb = esp_camera_fb_get();

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
 * - Camera setting updates requiring reinit
 *
 * NOTE: This is called AFTER config has been updated with new values.
 * The old config is no longer available for comparison.
 */
void ESPWiFi::cameraConfigHandler() {
#if ESPWiFi_HAS_CAMERA

  JsonObject camUpdate = configUpdate["camera"];
  if (camUpdate.isNull() || camUpdate.size() == 0) {
    return;
  }

  bool enabled = config["camera"]["enabled"].as<bool>();

  if (enabled) {
    log(INFO, "📷 Camera config changed, reinitializing...");
    deinitCamera();
    feedWatchDog(100);
    initCamera();
  } else {
    deinitCamera();
  }
#endif
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
#if !defined(CONFIG_HTTPD_WS_SUPPORT) || !ESPWiFi_HAS_CAMERA
  return;
#else
  if (!cameraSocStarted) {
    return;
  }

  // Check for subscribers (LAN + Cloud UI)
  // UI controls camera start/stop via WebSocket subscription
  const size_t lanSubs = (size_t)cameraStreamSubCount_;
  const bool cloudUiConnected =
      cloudMedia.isConnected() && cloudMedia.isRegistered();

  // If no subscribers, deinit camera and return
  if (lanSubs == 0 && !cloudUiConnected) {
    if (camera != nullptr) {
      deinitCamera();
    }
    return;
  }

  // Initialize camera if needed (only when we have subscribers)
  if (camera == nullptr) {
    if (!initCamera()) {
      return;
    }
  }

  static IntervalTimer frameTimer(1000);
  frameTimer.setIntervalMs((uint32_t)(1000 / 30)); // 30 FPS
  if (!frameTimer.shouldRunAt(esp_timer_get_time())) {
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb || fb->format != PIXFORMAT_JPEG || !fb->buf || fb->len == 0) {
    if (fb) {
      esp_camera_fb_return(fb);
    }
    return;
  }

  // Send to all LAN subscribers
  for (size_t i = 0; i < lanSubs; i++) {
    int fd = cameraStreamSubFds_[i];
    if (fd > 0) {
      cameraSoc.sendBinary(fd, fb->buf, fb->len);
    }
  }

  // Send to cloud tunnel if UI is connected via cloud and registered
  // (Cloud media tunnel only sends when there's an active UI connection)
  if (cloudMedia.isConnected() && cloudMedia.isRegistered()) {
    cloudMedia.sendBinary(fb->buf, fb->len);
  }

  esp_camera_fb_return(fb);
#endif
}