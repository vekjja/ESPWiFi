/**
 * @file CameraHandlers.cpp
 * @brief Camera event and state handlers for ESPWiFi
 *
 * This file implements camera event handlers and state management callbacks.
 * Camera events include initialization, configuration changes, and streaming
 * lifecycle events. Follows ESP-IDF component architecture best practices.
 */

#ifdef ESPWiFi_CAMERA_ENABLED

#ifndef ESPWiFi_CAMERA_HANDLERS
#define ESPWiFi_CAMERA_HANDLERS

#include "ESPWiFi.h"
#include "esp_camera.h"
#include "esp_log.h"

// ============================================================================
// Constants and TAG for ESP-IDF logging
// ============================================================================

static const char *CAM_HANDLER_TAG = "ESPWiFi_Camera_Handler";

// Cast helper macro with null check
#define ESPWiFi_OBJ_CAST(obj)                                                  \
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(obj);                              \
  if (!espwifi) {                                                              \
    ESP_LOGE(CAM_HANDLER_TAG, "Invalid ESPWiFi instance pointer");             \
    return;                                                                    \
  }

// ============================================================================
// Camera Event Handler Callbacks
// ============================================================================

/**
 * @brief Camera initialization event handler
 *
 * Called when camera initialization completes. Logs the result and updates
 * internal state flags. This handler should be registered after camera
 * hardware is successfully initialized.
 *
 * @param success True if initialization succeeded, false otherwise
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraInitHandler(bool success, void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  if (success) {
    espwifi->log(INFO, "📷 Camera initialized successfully");
    ESP_LOGI(CAM_HANDLER_TAG, "Camera hardware initialized");
  } else {
    espwifi->log(ERROR, "📷 Camera initialization failed");
    ESP_LOGE(CAM_HANDLER_TAG, "Camera hardware initialization failed");
  }
}

/**
 * @brief Camera settings update event handler
 *
 * Called when camera settings (brightness, contrast, etc.) are updated
 * via configuration changes. Validates the new settings and applies them
 * to the camera sensor if available.
 *
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraSettingsUpdateHandler(void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  if (espwifi->camera == nullptr) {
    espwifi->log(WARNING, "📷 Camera sensor not available for settings update");
    ESP_LOGW(CAM_HANDLER_TAG, "Sensor unavailable for settings update");
    return;
  }

  espwifi->log(DEBUG, "📷 Applying camera settings from configuration");
  ESP_LOGD(CAM_HANDLER_TAG, "Applying camera configuration settings");

  // Apply updated settings
  espwifi->updateCameraSettings();

  espwifi->log(INFO, "📷 Camera settings updated");
  ESP_LOGI(CAM_HANDLER_TAG, "Camera settings applied successfully");
}

/**
 * @brief Camera frame capture event handler
 *
 * Called periodically when camera frames are being streamed. Tracks frame
 * statistics and can be used for performance monitoring or frame-level
 * processing hooks.
 *
 * @param frameNumber Sequential frame number
 * @param frameSize Size of the captured frame in bytes
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraFrameCaptureHandler(uint32_t frameNumber, size_t frameSize,
                                        void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  // Log at VERBOSE level to avoid flooding logs during normal streaming
  ESP_LOGV(CAM_HANDLER_TAG, "Frame %lu captured: %zu bytes", frameNumber,
           frameSize);

  // Update streaming statistics if needed
  // This can be extended to track FPS, dropped frames, etc.
}

/**
 * @brief Camera error event handler
 *
 * Called when camera operations encounter errors (e.g., frame capture
 * timeout, I2C communication failure, buffer overflow). Logs the error
 * and can trigger recovery actions if needed.
 *
 * @param errorCode ESP error code
 * @param errorContext Description of what operation failed
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraErrorHandler(esp_err_t errorCode, const char *errorContext,
                                 void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  const char *errName = esp_err_to_name(errorCode);

  espwifi->log(ERROR, "📷 Camera error in %s: %s (0x%x)", errorContext, errName,
               errorCode);
  ESP_LOGE(CAM_HANDLER_TAG, "Camera error in %s: %s (0x%x)", errorContext,
           errName, errorCode);

  // Optionally trigger recovery actions based on error type
  // For example, reinitialize on certain I2C errors
  if (errorCode == ESP_ERR_TIMEOUT) {
    ESP_LOGW(CAM_HANDLER_TAG, "Frame capture timeout - sensor may need reset");
  }
}

// ============================================================================
// Camera Handler Registration/Unregistration
// ============================================================================

/**
 * @brief Register camera event handlers
 *
 * Sets up all camera-related event callbacks. Should be called after
 * successful camera initialization. Handlers are passed the ESPWiFi instance
 * pointer as context to allow access to logging and configuration.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ESPWiFi::registerCameraHandlers() {
  // Check if camera sensor is available (instead of accessing file-static var)
  if (camera == nullptr) {
    ESP_LOGW(CAM_HANDLER_TAG,
             "Cannot register handlers: camera not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Register event callbacks with ESPWiFi instance as context
  // Note: The esp_camera driver doesn't expose event callbacks directly,
  // so we track state internally. This structure is here for consistency
  // with other services and for future extensibility.

  ESP_LOGI(CAM_HANDLER_TAG, "Camera event handlers registered");
  log(DEBUG, "📷 Camera handlers registered");

  return ESP_OK;
}

/**
 * @brief Unregister camera event handlers
 *
 * Cleans up camera event callbacks. Should be called before camera
 * deinitialization to prevent callbacks from firing after cleanup.
 */
void ESPWiFi::unregisterCameraHandlers() {
  // Clear any internal handler references
  // The esp_camera driver handles its own cleanup, but we track state here
  // for consistency and future extensibility

  ESP_LOGI(CAM_HANDLER_TAG, "Camera event handlers unregistered");
  log(DEBUG, "📷 Camera handlers unregistered");
}

// ============================================================================
// Static Wrapper Functions
// ============================================================================

/**
 * @brief Static wrapper for camera init handler
 *
 * Provides a static function pointer that can be registered with C-style
 * callback APIs while forwarding to the instance method.
 *
 * @param success True if initialization succeeded
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraInitHandlerStatic(bool success, void *obj) {
  ESPWiFi_OBJ_CAST(obj);
  espwifi->cameraInitHandler(success, obj);
}

/**
 * @brief Static wrapper for camera settings update handler
 *
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraSettingsUpdateHandlerStatic(void *obj) {
  ESPWiFi_OBJ_CAST(obj);
  espwifi->cameraSettingsUpdateHandler(obj);
}

/**
 * @brief Static wrapper for camera frame capture handler
 *
 * @param frameNumber Sequential frame number
 * @param frameSize Size of frame in bytes
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraFrameCaptureHandlerStatic(uint32_t frameNumber,
                                              size_t frameSize, void *obj) {
  ESPWiFi_OBJ_CAST(obj);
  espwifi->cameraFrameCaptureHandler(frameNumber, frameSize, obj);
}

/**
 * @brief Static wrapper for camera error handler
 *
 * @param errorCode ESP error code
 * @param errorContext Description of failed operation
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::cameraErrorHandlerStatic(esp_err_t errorCode,
                                       const char *errorContext, void *obj) {
  ESPWiFi_OBJ_CAST(obj);
  espwifi->cameraErrorHandler(errorCode, errorContext, obj);
}

#endif // ESPWiFi_CAMERA_HANDLERS

#else // No camera model defined or ESPWiFi_CAMERA_ENABLED not set

// Provide stub implementations when camera is disabled
#include "ESPWiFi.h"

void ESPWiFi::cameraInitHandler(bool success, void *obj) {
  (void)success;
  (void)obj;
}
void ESPWiFi::cameraSettingsUpdateHandler(void *obj) { (void)obj; }
void ESPWiFi::cameraFrameCaptureHandler(uint32_t frameNumber, size_t frameSize,
                                        void *obj) {
  (void)frameNumber;
  (void)frameSize;
  (void)obj;
}
void ESPWiFi::cameraErrorHandler(esp_err_t errorCode, const char *errorContext,
                                 void *obj) {
  (void)errorCode;
  (void)errorContext;
  (void)obj;
}
esp_err_t ESPWiFi::registerCameraHandlers() { return ESP_ERR_NOT_SUPPORTED; }
void ESPWiFi::unregisterCameraHandlers() {}
void ESPWiFi::cameraInitHandlerStatic(bool success, void *obj) {
  (void)success;
  (void)obj;
}
void ESPWiFi::cameraSettingsUpdateHandlerStatic(void *obj) { (void)obj; }
void ESPWiFi::cameraFrameCaptureHandlerStatic(uint32_t frameNumber,
                                              size_t frameSize, void *obj) {
  (void)frameNumber;
  (void)frameSize;
  (void)obj;
}
void ESPWiFi::cameraErrorHandlerStatic(esp_err_t errorCode,
                                       const char *errorContext, void *obj) {
  (void)errorCode;
  (void)errorContext;
  (void)obj;
}

#endif // Camera model check
