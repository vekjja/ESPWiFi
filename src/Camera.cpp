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

bool ESPWiFi::initCamera() {
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    return true;
  }

  static bool initInProgress = false;
  if (initInProgress) {
    return false;
  }

  initInProgress = true;
  log("📷 Initializing camera...");

  if (ESP.getFreeHeap() < 50000) {
    logError("📷 Insufficient memory for camera init");
    initInProgress = false;
    return false;
  }

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
  camConfig.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    camConfig.frame_size = FRAMESIZE_SVGA;
    camConfig.jpeg_quality = 15;
    camConfig.fb_count = 4;
    camConfig.fb_location = CAMERA_FB_IN_PSRAM;
    camConfig.grab_mode = CAMERA_GRAB_LATEST;
    log("📷 Using PSRAM for camera buffers");
  } else {
    camConfig.frame_size = FRAMESIZE_QVGA;
    camConfig.jpeg_quality = 25;
    camConfig.fb_count = 2;
    camConfig.fb_location = CAMERA_FB_IN_DRAM;
    camConfig.grab_mode = CAMERA_GRAB_LATEST;
    log("📷 Using DRAM for camera buffers");
  }

  delay(100);
  yield();

  // Power up the camera if PWDN pin is defined
  if (camConfig.pin_pwdn != -1) {
    pinMode(camConfig.pin_pwdn, OUTPUT);
    digitalWrite(camConfig.pin_pwdn, LOW);
  }

  // Initialize the camera
  esp_err_t err = esp_camera_init(&camConfig);
  if (err != ESP_OK) {
    logError("📷 Camera Init Failed: " + String(err));
    initInProgress = false;
    return false;
  }

  log("📷 Camera initialized successfully");
  initInProgress = false;
  return true;
}

void ESPWiFi::deinitCamera() {
  log("📷 Deinitializing camera...");

  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    return;
  }

  if (isRecording) {
    stopVideoRecording();
  }

  clearCameraBuffer();
  yield();
  delay(100);

  // Deinitialize the camera
  esp_err_t err = esp_camera_deinit();
  if (err != ESP_OK) {
    logError("📷 Camera Deactivation Failed: " + String(err));
  }

  // Power down the camera if PWDN pin is defined
  if (camConfig.pin_pwdn != -1) {
    pinMode(camConfig.pin_pwdn, OUTPUT);
    digitalWrite(camConfig.pin_pwdn, HIGH);
  }

  log("📷 Camera deinitialized successfully");
  delay(200);
}

void ESPWiFi::startCamera() {
  if (!config["camera"]["enabled"]) {
    return;
  }

  if (!initCamera()) {
    logError("Skipping Camera Startup: Camera Initialization Failed");
    return;
  }

  initWebServer();
  webServer->on(
      "/camera/snapshot", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!lfs) {
          initLittleFS();
          if (!config.isNull() && config["sd"]["enabled"] == true) {
            initSDCard();
          }
        }

        String snapshotDir = "/snapshots";
        bool dirCreated = false;

        if (sdCardInitialized && sd) {
          if (!dirExists(sd, snapshotDir)) {
            dirCreated = mkDir(sd, snapshotDir);
          } else {
            dirCreated = true;
          }
        }

        if (!dirCreated && lfs) {
          if (!dirExists(lfs, snapshotDir)) {
            if (!mkDir(lfs, snapshotDir)) {
              request->send(500, "text/plain",
                            "Failed to create snapshots directory");
              return;
            }
          }
        } else if (!dirCreated) {
          request->send(500, "text/plain", "No file system available");
          return;
        }

        String filePath = snapshotDir + "/" + timestampForFilename() + ".jpg";
        takeSnapshot(filePath);

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

  // CORS preflight for record endpoints
  webServer->on(
      "/camera/record/start", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on(
      "/camera/record/stop", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

  webServer->on("/camera/record/start", HTTP_POST,
                [this](AsyncWebServerRequest *request) {
                  if (isRecording) {
                    AsyncWebServerResponse *response =
                        request->beginResponse(400, "application/json",
                                               "{\"error\":\"Recording already "
                                               "in progress\"}");
                    addCORS(response);
                    request->send(response);
                    return;
                  }
                  recordCamera();
                  AsyncWebServerResponse *response = request->beginResponse(
                      200, "application/json",
                      "{\"status\":\"Recording started\"}");
                  addCORS(response);
                  request->send(response);
                });

  webServer->on(
      "/camera/record/stop", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!isRecording) {
          AsyncWebServerResponse *response = request->beginResponse(
              400, "application/json",
              "{\"error\":\"No recording in progress\"}");
          addCORS(response);
          request->send(response);
          return;
        }
        stopVideoRecording();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json", "{\"status\":\"Recording stopped\"}");
        addCORS(response);
        request->send(response);
      });

  // CORS preflight for record status
  webServer->on(
      "/camera/record/status", HTTP_OPTIONS,
      [this](AsyncWebServerRequest *request) { handleCorsPreflight(request); });

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
                  AsyncWebServerResponse *httpResponse =
                      request->beginResponse(200, "application/json", response);
                  addCORS(httpResponse);
                  request->send(httpResponse);
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

  if (!sdCardInitialized) {
    initSDCard();

    if (!sdCardInitialized) {
      logError("SD card not available, cannot record camera");
      return;
    }
  }

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

  String filePath = recordingsDir + "/" + timestampForFilename() + ".mjpg";
  logf("📁 Recording path: %s\n", filePath.c_str());
  startVideoRecording(filePath);
}

void ESPWiFi::startVideoRecording(String filePath) {
  if (isRecording) {
    logError("Recording already in progress");
    return;
  }

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

  // Write simple MJPEG stream header
  // No complex AVI headers - just write JPEGs in sequence
  // This is a simple format that can be converted to proper video later
  // Start with a marker and frame rate info
  recordingFile.write((uint8_t *)"MJPEG", 5);
  recordingFile.write((uint8_t *)&recordingFrameRate,
                      sizeof(recordingFrameRate));

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

  static unsigned long lastFrameTime = 0;
  unsigned long frameInterval = 1000 / recordingFrameRate;

  if (millis() - lastFrameTime < frameInterval) {
    return;
  }
  lastFrameTime = millis();

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    return;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    esp_camera_fb_return(fb);
    return;
  }

  // Write frame size, then JPEG data
  uint32_t frameSize = fb->len;
  recordingFile.write((uint8_t *)&frameSize, sizeof(frameSize));

  size_t written = recordingFile.write(fb->buf, fb->len);
  if (written == fb->len) {
    recordingFrameCount++;
  }

  esp_camera_fb_return(fb);
}

void ESPWiFi::updateRecording() {
  if (isRecording) {
    recordFrame();
    if (millis() - recordingStartTime > 60000) {
      stopVideoRecording();
    }
  }
}

void ESPWiFi::clearCameraBuffer() {
  static bool firstClear = true;
  if (firstClear) {
    log("📷 Clearing camera buffer...");
    firstClear = false;
  }

  for (int i = 0; i < 3; i++) {
    camera_fb_t *temp_fb = esp_camera_fb_get();
    if (temp_fb) {
      esp_camera_fb_return(temp_fb);
    } else {
      break;
    }
    yield();
    delay(5);
  }

  delay(10);
}

void ESPWiFi::takeSnapshot(String filePath) {
  if (cameraOperationInProgress) {
    logError("📸 Camera operation already in progress, skipping snapshot");
    return;
  }

  if (!config["camera"]["enabled"]) {
    logError("📸 Camera not enabled, cannot take snapshot");
    return;
  }

  if (ESP.getFreeHeap() < 30000) {
    logError("📸 Insufficient memory for snapshot");
    return;
  }

  cameraOperationInProgress = true;
  clearCameraBuffer();
  yield();

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

  if (fb->len > 200000) {
    logError("📸 Snapshot too large: " + String(fb->len) + " bytes");
    esp_camera_fb_return(fb);
    cameraOperationInProgress = false;
    return;
  }

  log("📸 Snapshot Taken - Size: " + String(fb->len) + " bytes");

  bool writeSuccess = false;
  if (sdCardInitialized && sd) {
    writeSuccess = writeFile(sd, filePath, fb->buf, fb->len);
  }

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
  yield();
  cameraOperationInProgress = false;
}

void ESPWiFi::streamCamera(int frameRate) {
  if (cameraOperationInProgress || !config["camera"]["enabled"]) {
    return;
  }

  if (!this->camSoc || !this->camSoc->socket ||
      this->camSoc->numClients() == 0) {
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    return;
  }

  int targetFrameRate = 10;
  if (!config["camera"]["frameRate"].isNull()) {
    targetFrameRate = config["camera"]["frameRate"];
  } else if (frameRate > 0) {
    targetFrameRate = frameRate;
  }

  if (targetFrameRate > 8) {
    targetFrameRate = 8;
  }

  static IntervalTimer timer(1000);
  if (!timer.shouldRun(1000 / targetFrameRate)) {
    return;
  }

  yield();

  if (cameraOperationInProgress || !config["camera"]["enabled"] ||
      !this->camSoc) {
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError("📹 Failed to get camera frame buffer");
    return;
  }

  if (fb->buf && fb->len > 0 && fb->format == PIXFORMAT_JPEG) {
    if (this->camSoc && this->camSoc->socket && !cameraOperationInProgress) {
      if (fb->len > 100000) {
        logError("📹 Frame too large: " + String(fb->len) + " bytes");
        esp_camera_fb_return(fb);
        return;
      }

      if (config["camera"]["enabled"] && this->camSoc && this->camSoc->socket &&
          this->camSoc->numClients() > 0 && !cameraOperationInProgress) {
        this->camSoc->binaryAll((const char *)fb->buf, fb->len);
      }
    }
  } else {
    logError("📹 Invalid frame buffer - format: " + String(fb->format) +
             ", len: " + String(fb->len));
  }

  esp_camera_fb_return(fb);
  yield();
}

void ESPWiFi::cameraConfigHandler() {
  bool cameraEnabled = config["camera"]["enabled"];
  bool cameraCurrentlyRunning = (camSoc != nullptr);

  static unsigned long lastCameraToggle = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastCameraToggle < 2000) {
    log("📷 Camera toggle too frequent, waiting...");
    return;
  }

  if (cameraEnabled && !cameraCurrentlyRunning) {
    lastCameraToggle = currentTime;

    if (ESP.getFreeHeap() < 30000) {
      logError("📷 Insufficient memory for camera startup");
      return;
    }

    startCamera();
  } else if (!cameraEnabled && cameraCurrentlyRunning) {
    static bool shutdownInProgress = false;
    if (shutdownInProgress) {
      log("📷 Camera shutdown already in progress");
      return;
    }
    shutdownInProgress = true;

    lastCameraToggle = currentTime;

    if (camSoc && camSoc->socket) {
      log("📷 Disconnecting Camera WebSocket clients");
      camSoc->socket->closeAll();
      delay(100);
      webServer->removeHandler(camSoc->socket);
      camSoc->socket = nullptr;
    }

    cameraOperationInProgress = true;
    delay(100);

    if (isRecording) {
      log("📷 Stopping recording before camera disable");
      stopVideoRecording();
    }

    clearCameraBuffer();
    delay(100);

    if (camSoc) {
      delete camSoc;
      camSoc = nullptr;
    }

    deinitCamera();
    cameraOperationInProgress = false;
    shutdownInProgress = false;
  }
}

#endif // ESPWiFi_CAMERA
#endif // ESPWiFi_CAMERA_INSTALLED