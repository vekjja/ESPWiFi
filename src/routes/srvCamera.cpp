/**
 * @file srvCamera.cpp
 * @brief HTTP API routes for camera control and capture
 *
 * This module registers HTTP endpoints for camera operations:
 * - GET /api/camera/snapshot - Capture and return a single JPEG frame
 * - GET /api/camera/status - Get camera state and configuration info
 * - POST /api/camera/start - Initialize camera (if disabled)
 * - POST /api/camera/stop - Deinitialize camera
 * - WebSocket /ws/camera - Real-time JPEG frame streaming
 *
 * All routes require authentication and follow standard REST conventions.
 *
 * @note Camera routes are built when a camera model is selected (via
 *       ESPWiFi_CAMERA_MODEL_*; see `include/CameraPins.h`).
 * @note Requires ESPWiFi_CAMERA_MODEL_* to be defined (see
 * `include/CameraPins.h`)
 */

#include "ESPWiFi.h"

#if ESPWiFi_HAS_CAMERA

#ifndef ESPWiFi_SRV_CAMERA
#define ESPWiFi_SRV_CAMERA

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
#ifdef CONFIG_HTTPD_WS_SUPPORT
  // Start the websocket here
  if (!camSocStarted) {
    camSocStarted =
        camSoc.begin("/ws/camera", this,
                     /*onMessage*/ nullptr,
                     /*onConnect*/ nullptr,
                     /*onDisconnect*/ nullptr,
                     /*maxMessageLen*/ 512,
                     /*maxBroadcastLen*/ 128 * 1024,
                     /*requireAuth*/ false); // Disabled auth for testing

    if (!camSocStarted) {
      log(ERROR, "📷 Camera WebSocket failed to start");
    } else {
      log(INFO, "📷 Camera WebSocket successfully registered");
    }
  } else {
    log(WARNING, "📷 Camera WebSocket already registered, skipping");
  }
#endif

  registerRoute(
      "/api/camera/snapshot", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Validate pointers
        if (!espwifi || !req) {
          espwifi->log(ERROR, "📷 Invalid parameters to snapshot handler");
          return ESP_ERR_INVALID_ARG;
        }

        espwifi->log(ACCESS, "📷 Snapshot request from %s", clientInfo.c_str());

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
      "/api/camera/status", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        if (!espwifi || !req) {
          espwifi->log(ERROR, "Invalid parameters to status handler");
          return ESP_ERR_INVALID_ARG;
        }

        // Build status JSON
        JsonDocument statusDoc;

        // "installed" indicates camera hardware is physically available on
        // device
        statusDoc["installed"] =
            espwifi->config["camera"]["installed"].as<bool>();

        // "enabled" reflects actual runtime state: user enabled AND hardware
        // ready
        bool userEnabled = espwifi->config["camera"]["enabled"].as<bool>();
        bool hardwareReady = (espwifi->camera != nullptr);
        statusDoc["enabled"] = userEnabled && hardwareReady;

        // Frame rate configuration
        int frameRate = espwifi->config["camera"]["frameRate"].isNull()
                            ? 10
                            : espwifi->config["camera"]["frameRate"].as<int>();
        statusDoc["frameRate"] = frameRate;

        if (espwifi->camera != nullptr) {
          // Get sensor information
          camera_sensor_info_t *info =
              esp_camera_sensor_get_info(&espwifi->camera->id);
          if (info) {
            statusDoc["sensor"]["name"] = info->name;
            statusDoc["sensor"]["pid"] = espwifi->camera->id.PID;
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

        // PSRAM status (may be unavailable if PSRAM support is not enabled)
        bool psramAvailable = false;
#ifdef CONFIG_SPIRAM
        psramAvailable = esp_psram_is_initialized();
#endif
        statusDoc["psram"]["available"] = psramAvailable;
        if (psramAvailable) {
          size_t psramSize = 0;
#ifdef CONFIG_SPIRAM
          psramSize = esp_psram_get_size();
#endif
          statusDoc["psram"]["size"] = psramSize;
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
      "/api/camera/start", HTTP_POST,
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

        espwifi->log(INFO, "📷 Camera started via API");
        return espwifi->sendJsonResponse(req, 200, "{\"status\":\"started\"}",
                                         &clientInfo);
      });

  registerRoute("/api/camera/stop", HTTP_POST,
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
}

#endif // ESPWiFi_SRV_CAMERA

#else // No camera model selected

// Provide stub implementation when camera is disabled
void ESPWiFi::srvCamera() {
  // Camera not enabled - no routes registered
}

#endif // ESPWiFi_HAS_CAMERA
