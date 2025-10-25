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
  // Check if camera is already initialized
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    return true; // Camera already initialized
  }

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
  } else {
    // Without PSRAM - optimized for streaming
    camConfig.frame_size = FRAMESIZE_QVGA; // 320x240 for streaming
    camConfig.jpeg_quality = 25; // Higher compression for smaller frames
    camConfig.fb_count = 2;      // More buffers for smoother streaming
    camConfig.fb_location = CAMERA_FB_IN_DRAM;
    camConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // Better for streaming
  }

  esp_err_t err = esp_camera_init(&camConfig);
  if (err != ESP_OK) {
    String errMsg = String(err);
    logError("Camera Init Failed: " + errMsg + "\n");
    return false;
  }

  return true;
}

void ESPWiFi::deinitCamera() {
  // Check if camera is actually initialized before trying to deactivate
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    return; // Camera not initialized, nothing to deactivate
  }

  // Properly deactivate the camera hardware
  esp_err_t err = esp_camera_deinit();
  if (err != ESP_OK) {
    String errMsg = String(err);
    logError("Camera Deactivation Failed: " + errMsg);
  }
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

  webServer->on(
      "/camera/live", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String deviceName = config["mdns"];
        String html =
            "<!DOCTYPE html><html><head><title>" + deviceName +
            " Live Stream</title>"
            "<style>body{background:#181a1b;color:#e8eaed;font-family:sans-"
            "serif;text-align:center;}"
            "img{max-width:100vw;max-height:90vh;border-radius:8px;box-shadow:"
            "0 2px 8px #0008;margin-top:2em;}"
            "#status{margin:1em;font-size:1.1em;}"
            "</style>"
            "</head><body>"
            "<h2>&#128247; " +
            deviceName +
            " Live Stream</h2>"
            "<div id='status'>Connecting...</div>"
            "<img id='stream' alt='Camera Stream'/>"
            "<script>\n"
            "const img = document.getElementById('stream');\n"
            "const status = document.getElementById('status');\n"
            "let ws = new WebSocket((location.protocol === 'https:' ? 'wss://' "
            ": 'ws://') + location.host + '" +
            this->camSoc->socket->url() +
            "');\n"
            "ws.binaryType = 'arraybuffer';\n"
            "ws.onopen = () => { status.textContent = 'Connected!'; };\n"
            "ws.onclose = () => { status.textContent = 'Disconnected'; };\n"
            "ws.onerror = (e) => { status.textContent = 'WebSocket Error'; };\n"
            "ws.onmessage = (event) => {\n"
            "  if (event.data instanceof ArrayBuffer) {\n"
            "    let blob = new Blob([event.data], {type: 'image/jpeg'});\n"
            "    img.src = URL.createObjectURL(blob);\n"
            "    setTimeout(() => URL.revokeObjectURL(img.src), 1000);\n"
            "  }\n"
            "};\n"
            "</script>"
            "</body></html>";
        request->send(200, "text/html", html);
      });

  // Camera Settings API Endpoints
  webServer->on("/api/camera/settings", HTTP_GET,
                [this](AsyncWebServerRequest *request) {
                  // Return settings from config, with defaults as fallback
                  String response = "{";
                  response += "\"brightness\":" +
                              String(config["camera"]["brightness"].isNull()
                                         ? 1
                                         : config["camera"]["brightness"]) +
                              ",";
                  response += "\"contrast\":" +
                              String(config["camera"]["contrast"].isNull()
                                         ? 1
                                         : config["camera"]["contrast"]) +
                              ",";
                  response += "\"saturation\":" +
                              String(config["camera"]["saturation"].isNull()
                                         ? 1
                                         : config["camera"]["saturation"]) +
                              ",";
                  response += "\"exposure_level\":" +
                              String(config["camera"]["exposure_level"].isNull()
                                         ? 1
                                         : config["camera"]["exposure_level"]) +
                              ",";
                  response += "\"exposure_value\":" +
                              String(config["camera"]["exposure_value"].isNull()
                                         ? 400
                                         : config["camera"]["exposure_value"]) +
                              ",";
                  response += "\"agc_gain\":" +
                              String(config["camera"]["agc_gain"].isNull()
                                         ? 2
                                         : config["camera"]["agc_gain"]) +
                              ",";
                  response += "\"gain_ceiling\":" +
                              String(config["camera"]["gain_ceiling"].isNull()
                                         ? 2
                                         : config["camera"]["gain_ceiling"]) +
                              ",";
                  response += "\"white_balance\":" +
                              String(config["camera"]["white_balance"].isNull()
                                         ? 1
                                         : config["camera"]["white_balance"]) +
                              ",";
                  response += "\"awb_gain\":" +
                              String(config["camera"]["awb_gain"].isNull()
                                         ? 1
                                         : config["camera"]["awb_gain"]) +
                              ",";
                  response += "\"wb_mode\":" +
                              String(config["camera"]["wb_mode"].isNull()
                                         ? 0
                                         : config["camera"]["wb_mode"]) +
                              ",";
                  response += "\"frameRate\":" +
                              String(config["camera"]["frameRate"].isNull()
                                         ? 10
                                         : config["camera"]["frameRate"]);
                  response += "}";

                  AsyncWebServerResponse *resp =
                      request->beginResponse(200, "application/json", response);
                  addCORS(resp);
                  request->send(resp);
                });

  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/api/camera/settings",
      [this](AsyncWebServerRequest *request, JsonVariant &json) {
        sensor_t *s = esp_camera_sensor_get();
        if (s == NULL) {
          sendJsonResponse(request, 500,
                           "{\"error\":\"Camera not initialized\"}");
          return;
        }

        if (json.isNull()) {
          sendJsonResponse(request, 400, "{\"error\":\"Empty JSON\"}");
          return;
        }

        JsonDocument doc = json;
        bool configChanged = false;

        if (doc["brightness"].is<int>()) {
          int brightness = doc["brightness"];
          brightness = constrain(brightness, -2, 2);
          config["camera"]["brightness"] = brightness;
          s->set_brightness(s, brightness);
          configChanged = true;
        }
        if (doc["contrast"].is<int>()) {
          int contrast = doc["contrast"];
          contrast = constrain(contrast, -2, 2);
          config["camera"]["contrast"] = contrast;
          s->set_contrast(s, contrast);
          configChanged = true;
        }
        if (doc["saturation"].is<int>()) {
          int saturation = doc["saturation"];
          saturation = constrain(saturation, -2, 2);
          config["camera"]["saturation"] = saturation;
          s->set_saturation(s, saturation);
          configChanged = true;
        }
        if (doc["exposure_level"].is<int>()) {
          int ae_level = doc["exposure_level"];
          ae_level = constrain(ae_level, -2, 2);
          config["camera"]["exposure_level"] = ae_level;
          s->set_ae_level(s, ae_level);
          configChanged = true;
        }
        if (doc["exposure_value"].is<int>()) {
          int aec_value = doc["exposure_value"];
          aec_value = constrain(aec_value, 0, 1200);
          config["camera"]["exposure_value"] = aec_value;
          s->set_aec_value(s, aec_value);
          configChanged = true;
        }
        if (doc["agc_gain"].is<int>()) {
          int agc_gain = doc["agc_gain"];
          agc_gain = constrain(agc_gain, 0, 30);
          config["camera"]["agc_gain"] = agc_gain;
          s->set_agc_gain(s, agc_gain);
          configChanged = true;
        }
        if (doc["gain_ceiling"].is<int>()) {
          int gain_ceiling = doc["gain_ceiling"];
          gain_ceiling = constrain(gain_ceiling, 0, 6);
          config["camera"]["gain_ceiling"] = gain_ceiling;
          s->set_gainceiling(s, (gainceiling_t)gain_ceiling);
          configChanged = true;
        }
        if (doc["white_balance"].is<int>()) {
          int white_balance = doc["white_balance"];
          config["camera"]["white_balance"] = white_balance;
          s->set_whitebal(s, white_balance ? 1 : 0);
          configChanged = true;
        }
        if (doc["awb_gain"].is<int>()) {
          int awb_gain = doc["awb_gain"];
          config["camera"]["awb_gain"] = awb_gain;
          s->set_awb_gain(s, awb_gain ? 1 : 0);
          configChanged = true;
        }
        if (doc["wb_mode"].is<int>()) {
          int wb_mode = doc["wb_mode"];
          wb_mode = constrain(wb_mode, 0, 4);
          config["camera"]["wb_mode"] = wb_mode;
          s->set_wb_mode(s, wb_mode);
          configChanged = true;
        }
        if (doc["frameRate"].is<int>()) {
          int frameRate = doc["frameRate"];
          frameRate = constrain(frameRate, 1, 30);
          config["camera"]["frameRate"] = frameRate;
          configChanged = true;
        }

        // Save config if any settings changed
        if (configChanged) {
          log("📸 Camera settings being saved:");
          logf("  brightness: %d\n", config["camera"]["brightness"].as<int>());
          logf("  contrast: %d\n", config["camera"]["contrast"].as<int>());
          logf("  saturation: %d\n", config["camera"]["saturation"].as<int>());
          logf("  exposure_level: %d\n",
               config["camera"]["exposure_level"].as<int>());
          logf("  exposure_value: %d\n",
               config["camera"]["exposure_value"].as<int>());
          logf("  agc_gain: %d\n", config["camera"]["agc_gain"].as<int>());
          logf("  gain_ceiling: %d\n",
               config["camera"]["gain_ceiling"].as<int>());
          logf("  white_balance: %d\n",
               config["camera"]["white_balance"].as<int>());
          logf("  awb_gain: %d\n", config["camera"]["awb_gain"].as<int>());
          logf("  wb_mode: %d\n", config["camera"]["wb_mode"].as<int>());
          logf("  frameRate: %d\n", config["camera"]["frameRate"].as<int>());
          saveConfig();

          // Apply settings to already-initialized camera
          sensor_t *sensor = esp_camera_sensor_get();
          if (sensor != NULL) {
            sensor->set_brightness(sensor, config["camera"]["brightness"]);
            sensor->set_contrast(sensor, config["camera"]["contrast"]);
            sensor->set_saturation(sensor, config["camera"]["saturation"]);
            sensor->set_whitebal(sensor, config["camera"]["white_balance"]);
            sensor->set_awb_gain(sensor, config["camera"]["awb_gain"]);
            sensor->set_wb_mode(sensor, config["camera"]["wb_mode"]);
            sensor->set_ae_level(sensor, config["camera"]["exposure_level"]);
            sensor->set_aec_value(sensor, config["camera"]["exposure_value"]);
            sensor->set_agc_gain(sensor, config["camera"]["agc_gain"]);
            sensor->set_gainceiling(
                sensor, (gainceiling_t)config["camera"]["gain_ceiling"]);
            log("📸 Camera settings applied to sensor");
          }

          log("📸 Camera settings saved to config and applied");
        }

        sendJsonResponse(request, 200, "{\"success\":true}");
      }));

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

  log("📷 Camera Enabled");
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
  // Clear any cached frames multiple times to ensure clean buffer
  for (int i = 0; i < 5; i++) {
    camera_fb_t *temp_fb = esp_camera_fb_get();
    if (temp_fb) {
      esp_camera_fb_return(temp_fb);
    }
    delay(10); // Allow camera to settle between clears
  }
}

void ESPWiFi::takeSnapshot(String filePath) {
  // Safety check to prevent multiple simultaneous camera operations
  if (cameraOperationInProgress) {
    logError("📸 Camera operation already in progress, skipping snapshot");
    return;
  }

  cameraOperationInProgress = true;

  // Clear camera buffer to remove any cached frames
  clearCameraBuffer();

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
    logf("📸 Snapshot saved: %s (%s) at %s\n", filePath.c_str(),
         bytesToHumanReadable(fb->len).c_str(), timestamp().c_str());
  }

  esp_camera_fb_return(fb);

  // Clear the operation flag
  cameraOperationInProgress = false;
}

void ESPWiFi::streamCamera(int frameRate) {
  if (!config["camera"]["enabled"] || !this->camSoc ||
      this->camSoc->numClients() == 0) {
    if (!cameraOperationInProgress) {
      clearCameraBuffer();
    }
    return;
  }

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

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError("📹 Failed to get camera frame buffer");
    return;
  }

  // Validate frame buffer before sending
  if (fb->buf && fb->len > 0 && fb->format == PIXFORMAT_JPEG) {
    // Check if WebSocket is still valid before sending
    if (this->camSoc && this->camSoc->socket) {
      this->camSoc->binaryAll((const char *)fb->buf, fb->len);
    }
  } else {
    logError("📹 Invalid frame buffer - format: " + String(fb->format) +
             ", len: " + String(fb->len));
  }

  // Always return the frame buffer to prevent memory leaks
  esp_camera_fb_return(fb);
}

void ESPWiFi::cameraConfigHandler() {
  // Handle camera start/stop based on enabled state
  bool cameraEnabled = config["camera"]["enabled"];
  bool cameraCurrentlyRunning = (camSoc != nullptr);

  if (cameraEnabled && !cameraCurrentlyRunning) {
    // Camera should be enabled but not running - start it
    startCamera();
  } else if (!cameraEnabled && cameraCurrentlyRunning) {
    // Camera should be disabled but still running - stop it
    if (camSoc) {
      // Store reference to socket before cleanup
      AsyncWebSocket *socketToRemove = nullptr;
      if (camSoc->socket) {
        socketToRemove = camSoc->socket;

        // Close all client connections first
        socketToRemove->closeAll();

        // Remove the AsyncWebSocket handler from the server
        webServer->removeHandler(socketToRemove);

        // Clear the socket reference to prevent destructor from deleting it
        camSoc->socket = nullptr;
      }

      // Delete the WebSocket wrapper object
      delete camSoc;
      camSoc = nullptr;
    }

    // Properly deactivate the camera hardware
    deinitCamera();
    log("📷 Camera Disabled");
  }
}

#endif // ESPWiFi_CAMERA
#endif // ESPWiFi_CAMERA_INSTALLED