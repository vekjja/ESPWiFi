#ifdef ESPWiFi_CAMERA

#ifndef ESPWiFi_CAMERA_CPP
#define ESPWiFi_CAMERA_CPP

#include <Arduino.h>
#include <CameraPins.h>
#include <WebSocket.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_camera.h>

#include "ESPWiFi.h"
#include "base64.h"

void cameraWebSocketEventHandler(AsyncWebSocket *server,
                                 AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data,
                                 size_t len, ESPWiFi *espWifi) {
  if (!espWifi || !client) {
    return;
  }

  if (type == WS_EVT_DATA) {
    // String receivedData = String((char *)data, len);
    // receivedData.trim(); // Remove any whitespace
    // espWifi->log("🔌 WebSocket Data Received: 📨");
    // espWifi->logf("\tClient ID: %d\n", client->id());
    // espWifi->logf("\tData Length: %d bytes\n", len);
    // espWifi->logf("\tData: %s\n", receivedData.c_str());
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
  logln("📷 Initializing Camera");

  if (ESP.getFreeHeap() < 50000) {
    logError("📷 Insufficient Memory for Camera Initialization");
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
    logf("\tUsing PSRAM for camera buffers\n");
  } else {
    camConfig.frame_size = FRAMESIZE_QVGA;
    camConfig.jpeg_quality = 25;
    camConfig.fb_count = 2;
    camConfig.fb_location = CAMERA_FB_IN_DRAM;
    camConfig.grab_mode = CAMERA_GRAB_LATEST;
    logf("\tUsing DRAM for camera buffers\n");
  }

  delay(100);
  yield();

  // Power up the camera if PWDN pin is defined
  if (camConfig.pin_pwdn != -1) {
    pinMode(camConfig.pin_pwdn, OUTPUT);
    digitalWrite(camConfig.pin_pwdn, LOW);
    delay(100); // Give camera more time to power up
  }

  // Reset the camera if RESET pin is defined
  if (camConfig.pin_reset != -1) {
    pinMode(camConfig.pin_reset, OUTPUT);
    digitalWrite(camConfig.pin_reset, LOW);
    delay(10);
    digitalWrite(camConfig.pin_reset, HIGH);
    delay(50); // Longer delay after reset
  }

  // Check I2C bus for camera sensor before initialization (quiet check)
  // OV2640 typically at 0x30, OV5640/OV3660 at 0x3C, OV3660 sometimes at 0x60
  Wire.begin(SIOD_GPIO_NUM, SIOC_GPIO_NUM);
  delay(50);

  bool cameraDetected = false;
  uint8_t cameraAddress = 0;
  String cameraType = "Unknown";
  if (checkI2CDevice(0x30)) {
    logf("\tCamera detected on I2C at 0x30 (OV2640)\n");
    cameraDetected = true;
    cameraAddress = 0x30;
    cameraType = "OV2640";
  } else if (checkI2CDevice(0x3C)) {
    logf("\tCamera detected on I2C at 0x3C (OV5640/OV3660)\n");
    cameraDetected = true;
    cameraAddress = 0x3C;
    cameraType = "OV5640/OV3660";
  } else if (checkI2CDevice(0x60)) {
    logf("\tCamera detected on I2C at 0x60 (OV3660)\n");
    cameraDetected = true;
    cameraAddress = 0x60;
    cameraType = "OV3660";
  }
  // Only log if camera is detected - no need to log "not detected" messages

  delay(100);
  yield();

  // Try different combinations of XCLK frequencies and frame sizes
  int xclkFreqs[] = {20000000, 16000000, 10000000};
  framesize_t frameSizes[] = {FRAMESIZE_QQVGA, FRAMESIZE_QVGA, FRAMESIZE_CIF,
                              FRAMESIZE_VGA};
  const char *frameSizeNames[] = {"QQVGA", "QVGA", "CIF", "VGA"};
  esp_err_t err = ESP_FAIL;
  bool initSuccess = false;

  // First try with original frame size settings
  for (int freqIdx = 0; freqIdx < 3 && !initSuccess; freqIdx++) {
    camConfig.xclk_freq_hz = xclkFreqs[freqIdx];
    logf("\tTrying XCLK frequency: %d Hz, Frame: %s\n", camConfig.xclk_freq_hz,
         psramFound() ? "SVGA" : "QVGA");

    delay(100);
    yield();

    err = esp_camera_init(&camConfig);
    if (err == ESP_OK) {
      logf("\tCamera initialized successfully with XCLK: %d Hz\n",
           camConfig.xclk_freq_hz);
      initSuccess = true;
      break;
    } else {
      logf("\tCamera init failed with XCLK %d Hz, error: %d\n",
           camConfig.xclk_freq_hz, err);
      esp_camera_deinit();
      delay(50);
    }
  }

  // If that failed, try with smaller frame sizes
  if (!initSuccess) {
    logln("📷 Trying smaller frame sizes...");
    for (int sizeIdx = 0; sizeIdx < 4 && !initSuccess; sizeIdx++) {
      camConfig.frame_size = frameSizes[sizeIdx];
      if (psramFound()) {
        camConfig.fb_location = CAMERA_FB_IN_PSRAM;
        camConfig.fb_count = 2; // Reduce buffer count for smaller sizes
      } else {
        camConfig.fb_location = CAMERA_FB_IN_DRAM;
        camConfig.fb_count = 1; // Minimal buffer for DRAM
      }

      for (int freqIdx = 0; freqIdx < 3 && !initSuccess; freqIdx++) {
        camConfig.xclk_freq_hz = xclkFreqs[freqIdx];
        logf("\tTrying XCLK: %d Hz, Frame: %s\n", camConfig.xclk_freq_hz,
             frameSizeNames[sizeIdx]);

        delay(100);
        yield();

        err = esp_camera_init(&camConfig);
        if (err == ESP_OK) {
          logf("\tCamera initialized successfully!\n");
          logf("\tXCLK: %d Hz, Frame: %s\n", camConfig.xclk_freq_hz,
               frameSizeNames[sizeIdx]);
          initSuccess = true;
          break;
        } else {
          logf("\tFailed - XCLK: %d Hz, Frame: %s, Error: %d\n",
               camConfig.xclk_freq_hz, frameSizeNames[sizeIdx], err);
          esp_camera_deinit();
          delay(50);
        }
      }
    }
  }

  if (!initSuccess) {
    logError("📷 Camera Init Failed: " + String(err));
    logError("📷 Tried multiple XCLK frequencies and frame sizes");
    if (cameraDetected) {
      logError("📷 Camera detected on I2C but initialization failed");
      logError("📷 I2C address: 0x" + String(cameraAddress, HEX) + " (" +
               cameraType + ")");
    } else {
      logError("📷 Camera not detected on I2C bus - check connections");
    }
    initInProgress = false;
    return false;
  }

  // Get sensor info after successful initialization
  s = esp_camera_sensor_get();
  if (s != NULL) {
    logf("\tCamera sensor detected - ID: 0x%02X\n", s->id.PID);
    if (s->id.PID == 0x26) {
      logln("\tSensor type: OV2640");
    } else if (s->id.PID == 0x36) {
      logln("\tSensor type: OV3660");
    } else if (s->id.PID == 0x56) {
      logln("\tSensor type: OV5640");
    } else if (s->id.PID == 0x77) {
      logln("\tSensor type: OV7670");
    } else {
      logf("\tSensor type: Unknown (PID: 0x%02X)\n", s->id.PID);
    }
  }

  // Set camera settings from config
  updateCameraSettings();

  initInProgress = false;
  return true;
}

void ESPWiFi::updateCameraSettings() {
  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    logError("📷 Cannot Update Camera Settings: sensor not available");
    return;
  }

  // Apply camera settings from config
  if (!config["camera"]["brightness"].isNull()) {
    s->set_brightness(s, config["camera"]["brightness"]);
  }

  if (!config["camera"]["contrast"].isNull()) {
    s->set_contrast(s, config["camera"]["contrast"]);
  }

  if (!config["camera"]["saturation"].isNull()) {
    s->set_saturation(s, config["camera"]["saturation"]);
  }

  if (!config["camera"]["exposure_level"].isNull()) {
    s->set_ae_level(s, config["camera"]["exposure_level"]);
  }

  if (!config["camera"]["exposure_value"].isNull()) {
    s->set_aec_value(s, config["camera"]["exposure_value"]);
  }

  if (!config["camera"]["agc_gain"].isNull()) {
    s->set_agc_gain(s, config["camera"]["agc_gain"]);
  }

  if (!config["camera"]["gain_ceiling"].isNull()) {
    s->set_gainceiling(s, (gainceiling_t)config["camera"]["gain_ceiling"]);
  }

  if (!config["camera"]["white_balance"].isNull()) {
    s->set_whitebal(s, config["camera"]["white_balance"]);
  }

  if (!config["camera"]["awb_gain"].isNull()) {
    s->set_awb_gain(s, config["camera"]["awb_gain"]);
  }

  if (!config["camera"]["wb_mode"].isNull()) {
    s->set_wb_mode(s, config["camera"]["wb_mode"]);
  }

  if (!config["camera"]["rotation"].isNull()) {
    int rotation = config["camera"]["rotation"];
    if (rotation == 90) {
      s->set_vflip(s, 0);
      s->set_hmirror(s, 1);
    } else if (rotation == 180) {
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
    } else if (rotation == 270) {
      s->set_vflip(s, 1);
      s->set_hmirror(s, 0);
    } else {
      // 0 degrees or default
      s->set_vflip(s, 0);
      s->set_hmirror(s, 0);
    }
  }
  logln("📷 Camera Settings Updated");
}

void ESPWiFi::deinitCamera() {
  logln("📷 Deinitializing Camera");

  sensor_t *s = esp_camera_sensor_get();
  if (s == NULL) {
    return;
  }

  clearCameraBuffer();
  yield();
  delay(100);

  // Deinitialize the camera
  esp_err_t err = esp_camera_deinit();
  if (err != ESP_OK) {
    logError("📷 Camera Deinitialization Failed: " + String(err));
  }

  // Power down the camera if PWDN pin is defined
  if (camConfig.pin_pwdn != -1) {
    pinMode(camConfig.pin_pwdn, OUTPUT);
    digitalWrite(camConfig.pin_pwdn, HIGH);
  }

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
        if (!authorized(request)) {
          sendJsonResponse(request, 401, "{\"error\":\"Unauthorized\"}");
          return;
        }
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

  this->camSoc = new WebSocket("/camera", this, cameraWebSocketEventHandler);

  if (!this->camSoc) {
    logError("📷 Failed to create Camera WebSocket");
    return;
  }
}

void ESPWiFi::clearCameraBuffer() {
  static bool firstClear = true;
  if (firstClear) {
    logln("📷 Clearing Camera Buffer");
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

  logln("📸 Snapshot Taken");

  bool writeSuccess = false;
  if (sdCardInitialized && sd) {
    writeSuccess = writeFile(sd, filePath, fb->buf, fb->len);
  }

  if (!writeSuccess && lfs) {
    logln("📁 Falling back to LittleFS for Snapshot");
    writeSuccess = writeFile(lfs, filePath, fb->buf, fb->len);
  }

  if (!writeSuccess) {
    logError("📸 Failed to save snapshot to any filesystem");
  }

  esp_camera_fb_return(fb);
  yield();
  cameraOperationInProgress = false;
}

void ESPWiFi::streamCamera() {
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

  int targetFrameRate = 9;
  if (!config["camera"]["frameRate"].isNull()) {
    targetFrameRate = config["camera"]["frameRate"];
  }

  // Limit frame rate to 30fps
  if (targetFrameRate > 30) {
    targetFrameRate = 30;
  }

  static IntervalTimer timer(1000);
  if (!timer.shouldRun(1000 / targetFrameRate)) {
    return;
  }

  yield();

  // Checking again because config changes are asynchronous
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
      if (config["camera"]["enabled"] && this->camSoc && this->camSoc->socket &&
          this->camSoc->numClients() > 0 && !cameraOperationInProgress) {

        // Check if WebSocket has a large queue (buffer overflow protection)
        bool queueTooLarge = false;
        for (auto &client : this->camSoc->socket->getClients()) {
          if (client.queueIsFull()) {
            queueTooLarge = true;
            break;
          }
        }

        if (queueTooLarge) {
          esp_camera_fb_return(fb);
          return;
        }

        try {
          this->camSoc->binaryAll((const char *)fb->buf, fb->len);
        } catch (...) {
          logError("📹 Error sending frame data to WebSocket clients");
        }
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

  if (cameraEnabled && !cameraCurrentlyRunning) {

    startCamera();

  } else if (!cameraEnabled && cameraCurrentlyRunning) {
    static bool shutdownInProgress = false;
    if (shutdownInProgress) {
      return;
    }
    shutdownInProgress = true;

    if (camSoc && camSoc->socket) {
      logln("📷 Disconnecting Camera WebSocket clients");
      camSoc->socket->closeAll();
      delay(100);
      webServer->removeHandler(camSoc->socket);
      camSoc->socket = nullptr;
    }

    cameraOperationInProgress = true;

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

  if (cameraEnabled && cameraCurrentlyRunning) {
    updateCameraSettings();
  }
}

#endif // ESPWiFi_CAMERA_CPP
#endif // ESPWiFi_CAMERA