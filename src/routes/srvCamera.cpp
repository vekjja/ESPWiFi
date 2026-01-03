/**
 * @file srvCamera.cpp
 * @brief HTTP API routes for camera control and capture
 *
 * This module registers HTTP endpoints for camera operations:
 * - GET /camera/snapshot - Capture and return a single JPEG frame
 * - GET /camera/status - Get camera state and configuration info
 * - POST /camera/start - Initialize camera (if disabled)
 * - POST /camera/stop - Deinitialize camera
 *
 * All routes require authentication and follow standard REST conventions.
 *
 * @note Requires ESPWiFi_CAMERA_ENABLED build flag
 * @note Requires CAMERA_MODEL_* to be defined
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

#ifndef ESPWiFi_SRV_CAMERA
#define ESPWiFi_SRV_CAMERA

#include "ESPWiFi.h"
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_psram.h>

/**
 * @brief Register all camera-related HTTP routes
 *
 * Sets up RESTful endpoints for camera control. All routes require
 * authentication and use the standard route handler pattern.
 */
void ESPWiFi::srvCamera() {
  log(DEBUG, "Registering camera HTTP routes");

  registerRoute(
      "/camera/snapshot", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Validate pointers
        if (!espwifi || !req) {
          espwifi->log(ERROR, "📷 Invalid parameters to snapshot handler");
          return ESP_ERR_INVALID_ARG;
        }

        espwifi->log(INFO, "📷 Snapshot request from %s", clientInfo.c_str());

        // Check if camera is enabled in configuration
        if (!espwifi->config["camera"]["enabled"].as<bool>()) {
          espwifi->log(WARNING, "📷 Snapshot request but camera disabled");
          return espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"Camera disabled\"}", &clientInfo);
        }

        // Ensure camera is initialized
        if (!espwifi->initCamera()) {
          espwifi->log(ERROR, "📷 Failed to initialize camera for snapshot");
          return espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"Camera not available\"}", &clientInfo);
        }

        // Capture and send snapshot
        return espwifi->sendCameraSnapshot(req, clientInfo);
      });

  registerRoute(
      "/camera/status", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        if (!espwifi || !req) {
          espwifi->log(ERROR, "Invalid parameters to status handler");
          return ESP_ERR_INVALID_ARG;
        }

        espwifi->log(DEBUG, "📷 Status request from %s", clientInfo.c_str());

        // Build status JSON
        JsonDocument statusDoc;

        // Basic configuration
        statusDoc["enabled"] = espwifi->config["camera"]["enabled"].as<bool>();
        statusDoc["installed"] =
            espwifi->config["camera"]["installed"].as<bool>();

        // Frame rate configuration
        int frameRate = espwifi->config["camera"]["frameRate"].isNull()
                            ? 10
                            : espwifi->config["camera"]["frameRate"].as<int>();
        statusDoc["frameRate"] = frameRate;

        // Check if camera hardware is initialized
        sensor_t *sensor = esp_camera_sensor_get();
        statusDoc["initialized"] = (sensor != nullptr);

        if (sensor != nullptr) {
          // Get sensor information
          camera_sensor_info_t *info = esp_camera_sensor_get_info(&sensor->id);
          if (info) {
            statusDoc["sensor"]["name"] = info->name;
            statusDoc["sensor"]["pid"] = sensor->id.PID;
            statusDoc["sensor"]["model"] = info->model;
            statusDoc["sensor"]["max_size"] = info->max_size;
          }

          // Get current sensor settings
          statusDoc["settings"]["brightness"] =
              espwifi->config["camera"]["brightness"].as<int>();
          statusDoc["settings"]["contrast"] =
              espwifi->config["camera"]["contrast"].as<int>();
          statusDoc["settings"]["saturation"] =
              espwifi->config["camera"]["saturation"].as<int>();
        }

        // PSRAM status (ESP-IDF function)
        bool psramAvailable = esp_psram_is_initialized();
        statusDoc["psram"]["available"] = psramAvailable;
        if (psramAvailable) {
          statusDoc["psram"]["size"] = esp_psram_get_size();
          statusDoc["psram"]["free"] =
              heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        }

        // Serialize to string
        std::string jsonStr;
        serializeJson(statusDoc, jsonStr);

        espwifi->log(INFO, "📷 Returning camera status");
        return espwifi->sendJsonResponse(req, 200, jsonStr, &clientInfo);
      });

  registerRoute(
      "/camera/start", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        if (!espwifi || !req) {
          espwifi->log(ERROR, "Invalid parameters to start handler");
          return ESP_ERR_INVALID_ARG;
        }

        espwifi->log(INFO, "📷 Start request from %s", clientInfo.c_str());

        // Check if camera is marked as installed
        if (!espwifi->config["camera"]["installed"].as<bool>()) {
          espwifi->log(WARNING, "📷 Start request but camera not installed");
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Camera not installed\"}", &clientInfo);
        }

        // Initialize camera
        bool success = espwifi->initCamera();
        if (!success) {
          espwifi->log(ERROR, "📷 Failed to start camera");
          return espwifi->sendJsonResponse(
              req, 500, "{\"error\":\"Camera initialization failed\"}",
              &clientInfo);
        }

        // Start WebSocket endpoint
        espwifi->startCamera();

        espwifi->log(INFO, "📷 Camera started via API");
        return espwifi->sendJsonResponse(req, 200, "{\"status\":\"started\"}",
                                         &clientInfo);
      });

  registerRoute("/camera/stop", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  if (!espwifi || !req) {
                    espwifi->log(ERROR, "Invalid parameters to stop handler");
                    return ESP_ERR_INVALID_ARG;
                  }

                  espwifi->log(INFO, "📷 Stop request from %s",
                               clientInfo.c_str());

                  // Deinitialize camera
                  espwifi->deinitCamera();

                  espwifi->log(INFO, "📷 Camera stopped via API");
                  return espwifi->sendJsonResponse(
                      req, 200, "{\"status\":\"stopped\"}", &clientInfo);
                });

  log(DEBUG, "Camera routes registered successfully");
}

#endif // ESPWiFi_SRV_CAMERA

#else // No camera model defined or ESPWiFi_CAMERA_ENABLED not set

// Provide stub implementation when camera is disabled
#include "ESPWiFi.h"

void ESPWiFi::srvCamera() {
  // Camera not enabled - no routes registered
}

#endif // Camera model check
