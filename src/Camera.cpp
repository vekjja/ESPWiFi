#ifdef ESPWiFi_CAMERA_ENABLED

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

  // ESP32-CAM optimized settings
  if (psramFound()) {
    // With PSRAM - can use higher resolution
    camConfig.frame_size = FRAMESIZE_VGA; // 640x480
    camConfig.jpeg_quality = 20;
    camConfig.fb_count = 2;
    camConfig.fb_location = CAMERA_FB_IN_PSRAM;
    camConfig.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Without PSRAM - use smaller resolution to avoid memory issues
    camConfig.frame_size = FRAMESIZE_QVGA; // 320x240
    camConfig.jpeg_quality = 30;
    camConfig.fb_count = 1;
    camConfig.fb_location = CAMERA_FB_IN_DRAM;
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
        if (!fs) {
          initLittleFS();
          initSDCard();
        }

        String snapshotDir = "/snapshots";
        if (fs) {
          if (!dirExists(fs, snapshotDir)) {
            if (!mkDir(fs, snapshotDir)) {
              logError("Failed to create snapshots directory");
              request->send(500, "text/plain",
                            "Failed to create snapshots directory");
              return;
            }
          }
        } else {
          logError("No file system available for snapshots");
          request->send(500, "text/plain", "No file system available");
          return;
        }

        String filePath = snapshotDir + "/" + timestampForFilename() + ".jpg";
        takeSnapshot(filePath);

        // Use the fs pointer which points to the correct file system
        AsyncWebServerResponse *response =
            request->beginResponse(*fs, filePath, "image/jpeg");
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

  logf("📷 Camera Started\n");

  this->camSoc = new WebSocket(camSocPath, this, cameraWebSocketEventHandler);

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
  if (!fs) {
    initLittleFS();
    initSDCard();
  }

  if (!fs) {
    logError("No file system available for recording");
    return;
  }

  // Create recordings directory if it doesn't exist
  String recordingsDir = "/recordings";
  if (!dirExists(fs, recordingsDir)) {
    if (!mkDir(fs, recordingsDir)) {
      logError("Failed to create recordings directory");
      return;
    }
  }

  // Generate filename with timestamp
  String filePath = recordingsDir + "/" + timestampForFilename() + ".mjpeg";

  // Log which filesystem is being used
  String fsType = sdCardInitialized && fs ? "SD Card" : "LittleFS";
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

  recordingFile = fs->open(filePath, "w");
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
  unsigned long frameInterval =
      1000 / recordingFrameRate; // Convert FPS to milliseconds

  if (millis() - lastFrameTime < frameInterval) {
    return; // Skip this frame to maintain frame rate
  }
  lastFrameTime = millis();

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError("Camera Capture for Recording Failed");
    return;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    logError("Unsupported Pixel Format for Recording");
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
  if (written != fb->len) {
    logError("Failed to write complete frame to recording file");
  } else {
    recordingFrameCount++;
  }

  recordingFile.print("\r\n");

  esp_camera_fb_return(fb);
}

void ESPWiFi::updateRecording() {
  if (isRecording) {
    recordFrame();

    // Optional: Auto-stop recording after certain duration (e.g., 60 seconds)
    if (millis() - recordingStartTime > 60000) {
      stopVideoRecording();
    }
  }
}

void ESPWiFi::takeSnapshot(String filePath) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError(" Camera Capture Failed");
    return;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    logError("Unsupported Pixel Format");
    esp_camera_fb_return(fb);
    return;
  }

  // save the image using ESPWiFi::fs
  if (!fs) {
    logError("No file system available");
    esp_camera_fb_return(fb);
    return;
  }

  File file = fs->open(filePath, "w");
  if (!file) {
    logError("Failed to open file for writing");
  } else {
    size_t written = file.write(fb->buf, fb->len);
    if (written != fb->len) {
      logError("Failed to write complete image to file");
    } else {
      log("📸 Snapshot Saved: " + filePath);
    }
    file.close();
  }

  esp_camera_fb_return(fb);
}

void ESPWiFi::streamCamera(int frameRate) {
  if (!this->camSoc)
    return;
  if (this->camSoc->numClients() == 0)
    return;

  unsigned long interval =
      frameRate > 0 ? (1000 / frameRate)
                    : 500; // Default to 500ms if frameRate is 0 or negative

  static IntervalTimer timer(1000);
  if (!timer.shouldRun(interval)) {
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError("Camera Capture for Streaming Failed");
    return;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    logError("Unsupported Pixel Format");
    esp_camera_fb_return(fb);
    return;
  }

  // Validate buffer before sending
  if (fb->buf && fb->len > 0) {
    this->camSoc->binaryAll((const char *)fb->buf, fb->len);
  } else {
    logError("Invalid camera buffer");
  }

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
    log("📷 Camera Deactivated");
  }
}

#endif // ESPWiFi_CAMERA
#endif // ESPWiFi_CAMERA_ENABLED