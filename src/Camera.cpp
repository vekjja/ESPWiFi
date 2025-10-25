#ifdef ESPWiFi_CAMERA_INSTALLED

#ifndef ESPWiFi_CAMERA
#define ESPWiFi_CAMERA

#include <Arduino.h>
#include <CameraPins.h>
#include <WebSocket.h>
#include <WiFi.h>
#include <esp_camera.h>

#include "ESPWiFi.h"
#include "base64.h"

void cameraWebSocketEventHandler(AsyncWebSocket *server,
                                 AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data,
                                 size_t len, ESPWiFi *espWifi) {
  if (type == WS_EVT_DATA) {
    String receivedData = String((char *)data, len);
    receivedData.trim(); // Remove any whitespace
    espWifi->log("🔌 WebSocket Data Received: 📨");
    espWifi->logf("\tClient ID: %d\n", client->id());
    espWifi->logf("\tData Length: %d bytes\n", len);
    espWifi->logf("\tData: %s\n", receivedData.c_str());
  }
}

bool ESPWiFi::checkCameraGPIOConflicts() {
  // Skip GPIO conflict detection for now as it may interfere with camera pins
  // The camera pins are dedicated and should not be checked for conflicts
  // This was causing issues with the camera initialization
  return true;
}

bool ESPWiFi::initCamera() {
  // Check if camera is already initialized
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    log("📷 Camera already initialized");
    return true; // Camera already initialized
  }

  // Prevent multiple simultaneous initialization attempts
  static bool initInProgress = false;
  if (initInProgress) {
    log("📷 Camera initialization already in progress");
    return false;
  }

  initInProgress = true;
  log("📷 Initializing camera...");

  // Skip GPIO conflict detection to prevent interference
  // Camera pins are dedicated and should not be checked

  // Check available memory before initialization
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 50000) { // Need at least 50KB free heap
    logError("📷 Insufficient memory for camera init. Free heap: " +
             String(freeHeap));
    initInProgress = false;
    return false;
  }

  // Initialize camera configuration with proper error checking
  memset(&camConfig, 0, sizeof(camera_config_t));

  camConfig.ledc_channel = LEDC_CHANNEL_0;
  camConfig.ledc_timer = LEDC_TIMER_0;
  camConfig.pin_d0 = Y2_GPIO_NUM;
  camConfig.pin_d1 = Y3_GPIO_NUM;
  camConfig.pin_d2 = Y4_GPIO_NUM;
  camConfig.pin_d3 = Y5_GPIO_NUM;
  camConfig.pin_d4 = Y6_GPIO_NUM;
  camConfig.pin_d5 = Y7_GPIO_NUM;
  camConfig.pin_d6 = Y8_GPIO_NUM;
  camConfig.pin_d7 = Y9_GPIO_NUM;
  camConfig.pin_xclk = XCLK_GPIO_NUM;
  camConfig.pin_pclk = PCLK_GPIO_NUM;
  camConfig.pin_vsync = VSYNC_GPIO_NUM;
  camConfig.pin_href = HREF_GPIO_NUM;
  camConfig.pin_sccb_sda = SIOD_GPIO_NUM;
  camConfig.pin_sccb_scl = SIOC_GPIO_NUM;
  camConfig.pin_pwdn = PWDN_GPIO_NUM;
  camConfig.pin_reset = RESET_GPIO_NUM;
  camConfig.xclk_freq_hz = 20000000;
  camConfig.pixel_format = PIXFORMAT_JPEG; // for streaming

  // OV2640 optimized settings for streaming performance
  if (psramFound()) {
    // With PSRAM - use better settings for higher quality streaming
    camConfig.frame_size = FRAMESIZE_SVGA; // 800x600 for streaming with PSRAM
    camConfig.jpeg_quality = 15; // Better quality for streaming with PSRAM
    camConfig.fb_count = 4;      // More buffers for smoother streaming
    camConfig.fb_location = CAMERA_FB_IN_PSRAM;
    camConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // Better for streaming
    log("📷 Using PSRAM for camera buffers");
  } else {
    // Without PSRAM - optimized for streaming
    camConfig.frame_size = FRAMESIZE_QVGA; // 320x240 for streaming
    camConfig.jpeg_quality = 25; // Higher compression for smaller frames
    camConfig.fb_count = 2;      // More buffers for smoother streaming
    camConfig.fb_location = CAMERA_FB_IN_DRAM;
    camConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // Better for streaming
    log("📷 Using DRAM for camera buffers");
  }

  // Add small delay to ensure GPIOs are stable
  delay(100);

  // Feed watchdog before camera init
  yield();

  esp_err_t err = esp_camera_init(&camConfig);
  if (err != ESP_OK) {
    String errMsg = "Camera Init Failed: ";
    // Use generic error handling since specific camera error codes may not be
    // available
    if (err == ESP_ERR_NOT_FOUND) {
      errMsg += "Camera not detected";
    } else if (err == ESP_ERR_INVALID_ARG) {
      errMsg += "Invalid camera configuration";
    } else if (err == ESP_ERR_NO_MEM) {
      errMsg += "Insufficient memory for camera";
    } else if (err == ESP_ERR_INVALID_STATE) {
      errMsg += "Camera in invalid state";
    } else {
      errMsg += "Error code: " + String(err);
    }
    logError(errMsg);
    initInProgress = false;
    return false;
  }

  log("📷 Camera initialized successfully");
  initInProgress = false;
  return true;
}

void ESPWiFi::deinitCamera() {
  log("📷 Deinitializing camera...");

  // Check if camera is actually initialized before trying to deactivate
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    log("📷 Camera not initialized, nothing to deactivate");
    return; // Camera not initialized, nothing to deactivate
  }

  // Stop any ongoing camera operations
  if (isRecording) {
    log("📷 Stopping recording before camera deinit");
    stopVideoRecording();
  }

  // Clear camera buffer to free any pending frames
  clearCameraBuffer();

  // Feed watchdog before deinit
  yield();
  delay(100); // Longer delay to ensure all operations complete

  // Properly deactivate the camera hardware
  esp_err_t err = esp_camera_deinit();
  if (err != ESP_OK) {
    String errMsg = "Camera Deactivation Failed: ";
    // Use generic error handling since specific camera error codes may not be
    // available
    if (err == ESP_ERR_NOT_FOUND) {
      errMsg += "Camera not initialized";
    } else if (err == ESP_ERR_INVALID_STATE) {
      errMsg += "Camera in invalid state";
    } else if (err == ESP_ERR_INVALID_ARG) {
      errMsg += "Invalid camera state";
    } else {
      errMsg += "Error code: " + String(err);
    }
    logError(errMsg);
  }

  // Additional cleanup delay
  delay(200); // Longer delay for complete shutdown
}

void ESPWiFi::startCamera() {
  if (!config["camera"]["enabled"]) {
    return;
  }

  // Initialize camera hardware
  if (!initCamera()) {
    logError("Skipping Camera Startup: Camera Initialization Failed");
    return;
  }

  initWebServer();
  webServer->on(
      "/camera/snapshot", HTTP_GET, [this](AsyncWebServerRequest *request) {
        // Ensure file system is initialized
        if (!lfs) {
          initLittleFS();
          if (!config.isNull() && config["sd"]["enabled"] == true) {
            initSDCard();
          }
        }

        String snapshotDir = "/snapshots";
        // Create directory on SD card first, fallback to LittleFS
        bool dirCreated = false;
        if (sdCardInitialized && sd) {
          if (!dirExists(sd, snapshotDir)) {
            if (mkDir(sd, snapshotDir)) {
              dirCreated = true;
            }
          } else {
            dirCreated = true;
          }
        }

        // Fallback to LittleFS if SD card failed
        if (!dirCreated && lfs) {
          if (!dirExists(lfs, snapshotDir)) {
            if (!mkDir(lfs, snapshotDir)) {
              logError("Failed to create snapshots directory on LittleFS");
              request->send(500, "text/plain",
                            "Failed to create snapshots directory");
              return;
            }
          }
        } else if (!dirCreated) {
          logError("No file system available for snapshots");
          request->send(500, "text/plain", "No file system available");
          return;
        }

        String filePath = snapshotDir + "/" + timestampForFilename() + ".jpg";
        takeSnapshot(filePath);

        // Serve from SD card first, then LittleFS
        AsyncWebServerResponse *response;
        if (sdCardInitialized && sd && sd->exists(filePath)) {
          response = request->beginResponse(*sd, filePath, "image/jpeg");
        } else if (lfs && lfs->exists(filePath)) {
          response = request->beginResponse(*lfs, filePath, "image/jpeg");
        } else {
          request->send(404, "text/plain", "Snapshot not found");
          return;
        }
        response->addHeader("Content-Disposition",
                            "inline; filename=" + filePath);
        addCORS(response);
        request->send(response);
      });

  // Video Recording Endpoints
  webServer->on("/camera/record/start", HTTP_POST,
                [this](AsyncWebServerRequest *request) {
                  if (isRecording) {
                    request->send(
                        400, "application/json",
                        "{\"error\":\"Recording already in progress\"}");
                    return;
                  }

                  recordCamera();
                  request->send(200, "application/json",
                                "{\"status\":\"Recording started\"}");
                });

  webServer->on("/camera/record/stop", HTTP_POST,
                [this](AsyncWebServerRequest *request) {
                  if (!isRecording) {
                    request->send(400, "application/json",
                                  "{\"error\":\"No recording in progress\"}");
                    return;
                  }

                  stopVideoRecording();
                  request->send(200, "application/json",
                                "{\"status\":\"Recording stopped\"}");
                });

  webServer->on("/camera/record/status", HTTP_GET,
                [this](AsyncWebServerRequest *request) {
                  String status = isRecording ? "recording" : "stopped";
                  String response = "{\"status\":\"" + status + "\"";

                  if (isRecording) {
                    response += ",\"file\":\"" + recordingFilePath + "\"";
                    response += ",\"frames\":" + String(recordingFrameCount);
                    response += ",\"duration\":" +
                                String(millis() - recordingStartTime);
                  }

                  response += "}";
                  request->send(200, "application/json", response);
                });

  this->camSoc = new WebSocket("/camera", this, cameraWebSocketEventHandler);

  if (!this->camSoc) {
    logError(" Failed to create Camera WebSocket");
    return;
  }
}

void ESPWiFi::recordCamera() {
  if (!config["camera"]["enabled"]) {
    return;
  }

  if (!initCamera()) {
    logError("Skipping Camera Recording: Camera Initialization Failed");
    return;
  }

  // Ensure file system is initialized
  if (!lfs) {
    initLittleFS();
    if (!config.isNull() && config["sd"]["enabled"] == true) {
      initSDCard();
    }
  }

  if (!sdCardInitialized) {
    logError("SD card not available for recording");
    return;
  }

  // Create recordings directory if it doesn't exist - SD card only
  String recordingsDir = "/recordings";
  if (sdCardInitialized && sd) {
    if (!dirExists(sd, recordingsDir)) {
      if (!mkDir(sd, recordingsDir)) {
        logError("Failed to create recordings directory on SD card");
        return;
      }
    }
  } else {
    logError("SD card not available for recordings");
    return;
  }

  // Generate filename with timestamp
  String filePath = recordingsDir + "/" + timestampForFilename() + ".mjpeg";

  // Log which filesystem is being used
  String fsType = "SD Card";
  logf("📁 Using filesystem: %s\n", fsType.c_str());
  logf("📁 Recording path: %s\n", filePath.c_str());

  // Start recording
  startVideoRecording(filePath);
}

void ESPWiFi::startVideoRecording(String filePath) {
  if (isRecording) {
    logError("Recording already in progress");
    return;
  }

  // Use SD card for recording, error if not available
  if (sdCardInitialized && sd) {
    recordingFile = sd->open(filePath, "w");
  } else {
    logError("SD card not available for recording");
    return;
  }
  if (!recordingFile) {
    logError("Failed to open recording file: " + filePath);
    return;
  }

  // Write MJPEG header
  recordingFile.print("HTTP/1.1 200 OK\r\n");
  recordingFile.print(
      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n");
  recordingFile.print("\r\n");

  isRecording = true;
  recordingFilePath = filePath;
  recordingStartTime = millis();
  recordingFrameCount = 0;

  log("🎥 Started video recording: " + filePath);
}

void ESPWiFi::stopVideoRecording() {
  if (!isRecording) {
    return;
  }

  if (recordingFile) {
    recordingFile.close();
  }

  isRecording = false;
  unsigned long duration = millis() - recordingStartTime;

  log("🎥 Stopped video recording: " + recordingFilePath);
  logf("📊 Recording stats: %d frames, %lu ms duration\n", recordingFrameCount,
       duration);

  recordingFilePath = "";
  recordingFrameCount = 0;
}

void ESPWiFi::recordFrame() {
  if (!isRecording || !recordingFile) {
    return;
  }

  // Check frame rate timing
  static unsigned long lastFrameTime = 0;
  unsigned long frameInterval = 1000 / recordingFrameRate;

  if (millis() - lastFrameTime < frameInterval) {
    return; // Skip this frame to maintain frame rate
  }
  lastFrameTime = millis();

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    return; // Skip error logging for performance
  }

  if (fb->format != PIXFORMAT_JPEG) {
    esp_camera_fb_return(fb);
    return;
  }

  // Write frame boundary and headers
  recordingFile.print("--frame\r\n");
  recordingFile.print("Content-Type: image/jpeg\r\n");
  recordingFile.print("Content-Length: " + String(fb->len) + "\r\n");
  recordingFile.print("\r\n");

  // Write frame data
  size_t written = recordingFile.write(fb->buf, fb->len);
  if (written == fb->len) {
    recordingFrameCount++;
  }

  recordingFile.print("\r\n");
  esp_camera_fb_return(fb);
}

void ESPWiFi::updateRecording() {
  if (isRecording) {
    recordFrame();

    // Auto-stop recording after 60 seconds to prevent storage overflow
    if (millis() - recordingStartTime > 60000) {
      stopVideoRecording();
    }
  }
}

void ESPWiFi::clearCameraBuffer() {
  // Only log once to prevent spam
  static bool firstClear = true;
  if (firstClear) {
    log("📷 Clearing camera buffer...");
    firstClear = false;
  }

  // Clear any cached frames with timeout protection
  for (int i = 0; i < 3; i++) { // Reduced from 5 to 3 iterations
    camera_fb_t *temp_fb = esp_camera_fb_get();
    if (temp_fb) {
      esp_camera_fb_return(temp_fb);
    } else {
      // No more frames available, break early
      break;
    }

    // Feed watchdog during buffer clearing
    yield();
    delay(5); // Reduced delay from 10ms to 5ms
  }

  // Shorter delay to prevent blocking
  delay(10); // Reduced from 50ms to 10ms
}

void ESPWiFi::takeSnapshot(String filePath) {
  // Safety check to prevent multiple simultaneous camera operations
  if (cameraOperationInProgress) {
    logError("📸 Camera operation already in progress, skipping snapshot");
    return;
  }

  // Check if camera is enabled
  if (!config["camera"]["enabled"]) {
    logError("📸 Camera not enabled, cannot take snapshot");
    return;
  }

  // Check memory before snapshot
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 30000) { // Need at least 30KB free heap for snapshot
    logError("📸 Insufficient memory for snapshot. Free heap: " +
             String(freeHeap));
    return;
  }

  cameraOperationInProgress = true;

  // Clear camera buffer to remove any cached frames
  clearCameraBuffer();

  // Feed watchdog before camera operation
  yield();

  // Get the final fresh frame for the snapshot
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError("📸 Camera Capture Failed");
    cameraOperationInProgress = false;
    return;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    logError("📸 Unsupported Pixel Format");
    esp_camera_fb_return(fb);
    cameraOperationInProgress = false;
    return;
  }

  // Check frame size to prevent memory issues
  if (fb->len > 200000) { // Limit snapshot size to 200KB
    logError("📸 Snapshot too large: " + String(fb->len) + " bytes");
    esp_camera_fb_return(fb);
    cameraOperationInProgress = false;
    return;
  }

  log("📸 Snapshot Taken - Size: " + String(fb->len) + " bytes");

  // Try SD card first, fallback to LittleFS for snapshots
  bool writeSuccess = false;
  if (sdCardInitialized && sd) {
    writeSuccess = writeFile(sd, filePath, fb->buf, fb->len);
  }

  // Fallback to LittleFS if SD card failed or not available
  if (!writeSuccess && lfs) {
    log("📁 Falling back to LittleFS for snapshot");
    writeSuccess = writeFile(lfs, filePath, fb->buf, fb->len);
  }

  if (!writeSuccess) {
    logError("📸 Failed to save snapshot to any filesystem");
  } else {
    log("📸 Snapshot saved successfully");
  }

  esp_camera_fb_return(fb);

  // Feed watchdog after operation
  yield();

  // Clear the operation flag
  cameraOperationInProgress = false;
}

void ESPWiFi::streamCamera(int frameRate) {
  // CRITICAL: Check if camera operations are blocked (e.g., during shutdown)
  if (cameraOperationInProgress) {
    return;
  }

  // Check if camera is enabled first
  if (!config["camera"]["enabled"]) {
    return;
  }

  // CRITICAL: Check if WebSocket is being disconnected
  if (!this->camSoc || !this->camSoc->socket) {
    return;
  }

  // Check if WebSocket exists and has clients
  if (!this->camSoc || !this->camSoc->socket ||
      this->camSoc->numClients() == 0) {
    return;
  }

  // Double-check camera is still initialized (race condition protection)
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    return; // Camera not initialized, can't stream
  }

  // Check memory before streaming
  // size_t freeHeap = ESP.getFreeHeap();
  // if (freeHeap < 20000) { // Need at least 20KB free heap for streaming
  //   logError("📹 Insufficient memory for camera streaming. Free heap: " +
  //            String(freeHeap));
  //   return;
  // }

  // Use frame rate from config, fallback to parameter, then default
  int targetFrameRate = 10; // Default
  if (!config["camera"]["frameRate"].isNull()) {
    targetFrameRate = config["camera"]["frameRate"];
  } else if (frameRate > 0) {
    targetFrameRate = frameRate;
  }

  // Cap at reasonable maximum to prevent WebSocket overflow
  if (targetFrameRate > 8)
    targetFrameRate = 8;

  unsigned long interval = 1000 / targetFrameRate;

  static IntervalTimer timer(1000);
  if (!timer.shouldRun(interval)) {
    return;
  }

  // Feed watchdog before camera operation
  yield();

  // CRITICAL: Final safety check before camera operation
  // This prevents crashes during shutdown race conditions
  if (cameraOperationInProgress || !config["camera"]["enabled"] ||
      !this->camSoc) {
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError("📹 Failed to get camera frame buffer");
    return;
  }

  // Validate frame buffer before sending
  if (fb->buf && fb->len > 0 && fb->format == PIXFORMAT_JPEG) {
    // CRITICAL: Final check before WebSocket send
    // This prevents crashes if WebSocket was deleted during frame capture
    if (this->camSoc && this->camSoc->socket && !cameraOperationInProgress) {
      // Check frame size to prevent memory issues
      if (fb->len > 100000) { // Limit frame size to 100KB
        logError("📹 Frame too large: " + String(fb->len) + " bytes");
        esp_camera_fb_return(fb);
        return;
      }

      // Final safety check before sending
      if (config["camera"]["enabled"] && this->camSoc && this->camSoc->socket &&
          this->camSoc->numClients() > 0 && !cameraOperationInProgress) {
        this->camSoc->binaryAll((const char *)fb->buf, fb->len);
      }
    }
  } else {
    logError("📹 Invalid frame buffer - format: " + String(fb->format) +
             ", len: " + String(fb->len));
  }

  // Always return the frame buffer to prevent memory leaks
  esp_camera_fb_return(fb);

  // Feed watchdog after operation
  yield();
}

void ESPWiFi::cameraConfigHandler() {
  // Handle camera start/stop based on enabled state
  bool cameraEnabled = config["camera"]["enabled"];
  bool cameraCurrentlyRunning = (camSoc != nullptr);

  // Prevent rapid init/deinit cycles that can cause restarts
  static unsigned long lastCameraToggle = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastCameraToggle < 2000) { // 2 second cooldown
    log("📷 Camera toggle too frequent, waiting...");
    return;
  }

  if (cameraEnabled && !cameraCurrentlyRunning) {
    log("📷 Starting camera...");
    lastCameraToggle = currentTime;

    // Check memory before starting camera
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 30000) {
      logError("📷 Insufficient memory for camera startup. Free heap: " +
               String(freeHeap));
      return;
    }

    startCamera();
  } else if (!cameraEnabled && cameraCurrentlyRunning) {
    // Prevent multiple shutdown attempts
    static bool shutdownInProgress = false;
    if (shutdownInProgress) {
      log("📷 Camera shutdown already in progress");
      return;
    }
    shutdownInProgress = true;

    log("📷 Stopping camera...");
    lastCameraToggle = currentTime;

    // CRITICAL: Disconnect WebSocket clients FIRST to stop streaming
    if (camSoc && camSoc->socket) {
      log("📷 Disconnecting WebSocket clients...");

      // Close all client connections immediately
      camSoc->socket->closeAll();

      // Wait for connections to close
      delay(100);

      // Remove the AsyncWebSocket handler from the server
      webServer->removeHandler(camSoc->socket);

      // Clear the socket reference
      camSoc->socket = nullptr;
    }

    // Now set flag to prevent new camera operations
    cameraOperationInProgress = true;

    // Wait for any ongoing operations to complete
    delay(100);

    // Stop any ongoing recording
    if (isRecording) {
      log("📷 Stopping recording before camera disable");
      stopVideoRecording();
    }

    // Clear camera buffer to free any pending frames
    clearCameraBuffer();

    // Wait for operations to complete
    delay(100);

    // Now safely delete WebSocket object
    if (camSoc) {
      delete camSoc;
      camSoc = nullptr;
    }

    // Properly deactivate the camera hardware
    deinitCamera();

    // Reset operation flag
    cameraOperationInProgress = false;

    // Reset shutdown flag
    shutdownInProgress = false;
  }
}

#endif // ESPWiFi_CAMERA
#endif // ESPWiFi_CAMERA_INSTALLED