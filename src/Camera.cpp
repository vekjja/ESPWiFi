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
    // espWifi->log("\tClient ID: %d", client->id());
    // espWifi->log("\tData Length: %d bytes", len);
    // espWifi->log("\tData: %s", receivedData.c_str());
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
  logInfo("📷 Initializing Camera");

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

  bool usingPSRAM = psramFound();
  if (usingPSRAM) {
    camConfig.frame_size = FRAMESIZE_SVGA;
    camConfig.jpeg_quality = 15;
    camConfig.fb_count = 4;
    camConfig.fb_location = CAMERA_FB_IN_PSRAM;
    camConfig.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    camConfig.frame_size = FRAMESIZE_QVGA;
    camConfig.jpeg_quality = 25;
    camConfig.fb_count = 2;
    camConfig.fb_location = CAMERA_FB_IN_DRAM;
    camConfig.grab_mode = CAMERA_GRAB_LATEST;
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
    logDebug("\tCamera detected on I2C at 0x30 (OV2640)");
    cameraDetected = true;
    cameraAddress = 0x30;
    cameraType = "OV2640";
  } else if (checkI2CDevice(0x3C)) {
    logDebug("\tCamera detected on I2C at 0x3C (OV5640/OV3660)");
    cameraDetected = true;
    cameraAddress = 0x3C;
    cameraType = "OV5640/OV3660";
  } else if (checkI2CDevice(0x60)) {
    logDebug("\tCamera detected on I2C at 0x60 (OV3660)");
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

    delay(100);
    yield();

    err = esp_camera_init(&camConfig);
    if (err == ESP_OK) {
      initSuccess = true;
      break;
    } else {
      logError("\tFailed XCLK %d Hz (error: %d)", camConfig.xclk_freq_hz, err);
      esp_camera_deinit();
      delay(50);
    }
  }

  // If that failed, try with smaller frame sizes
  if (!initSuccess) {
    logWarn("📷 Trying smaller frame sizes...");
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

        delay(100);
        yield();

        err = esp_camera_init(&camConfig);
        if (err == ESP_OK) {
          initSuccess = true;
          break;
        } else {
          logError("\tFailed XCLK %d Hz, Frame %s (error: %d)",
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
    const char *frameSizeName = psramFound() ? "SVGA" : "QVGA";
    String sensorType = "Unknown";
    uint8_t pid = s->id.PID;

    if (pid == 0x26) {
      sensorType = "OV2640";
    } else if (pid == 0x36) {
      sensorType = "OV3660";
    } else if (pid == 0x56) {
      sensorType = "OV5640";
    } else if (pid == 0x77) {
      sensorType = "OV7670";
    }

    // Construct full sensor ID (VER:PID as 16-bit value)
    uint8_t ver = s->id.VER;
    uint16_t fullSensorId = ((uint16_t)ver << 8) | pid;

    logInfo("📷 Camera initialized:");
    logDebug("\tType: %s", sensorType.c_str());
    logDebug("\tID: 0x%04X", fullSensorId);
    logDebug("\tPID: 0x%02X", pid);
    logDebug("\tXCLK: %d Hz", camConfig.xclk_freq_hz);
    logDebug("\tFrame: %s", frameSizeName);
    logDebug("\t%s", usingPSRAM ? "PSRAM" : "DRAM");
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

  logInfo("📷 Camera Settings Updated");
  // Apply camera settings from config
  if (!config["camera"]["brightness"].isNull()) {
    int brightness = config["camera"]["brightness"];
    s->set_brightness(s, brightness);
    logDebug("\tBrightness: %d", brightness);
  }

  if (!config["camera"]["contrast"].isNull()) {
    int contrast = config["camera"]["contrast"];
    s->set_contrast(s, contrast);
    logDebug("\tContrast: %d", contrast);
  }

  if (!config["camera"]["saturation"].isNull()) {
    int saturation = config["camera"]["saturation"];
    s->set_saturation(s, saturation);
    logDebug("\tSaturation: %d", saturation);
  }

  if (!config["camera"]["exposure_level"].isNull()) {
    int exposureLevel = config["camera"]["exposure_level"];
    s->set_ae_level(s, exposureLevel);
    logDebug("\tExposure Level: %d", exposureLevel);
  }

  if (!config["camera"]["exposure_value"].isNull()) {
    int exposureValue = config["camera"]["exposure_value"];
    s->set_aec_value(s, exposureValue);
    logDebug("\tExposure Value: %d", exposureValue);
  }

  if (!config["camera"]["agc_gain"].isNull()) {
    int agcGain = config["camera"]["agc_gain"];
    s->set_agc_gain(s, agcGain);
    logDebug("\tAGC Gain: %d", agcGain);
  }

  if (!config["camera"]["gain_ceiling"].isNull()) {
    int gainCeiling = config["camera"]["gain_ceiling"];
    s->set_gainceiling(s, (gainceiling_t)gainCeiling);
    logDebug("\tGain Ceiling: %d", gainCeiling);
  }

  if (!config["camera"]["white_balance"].isNull()) {
    int whiteBalance = config["camera"]["white_balance"];
    s->set_whitebal(s, whiteBalance);
    logDebug("\tWhite Balance: %d", whiteBalance);
  }

  if (!config["camera"]["awb_gain"].isNull()) {
    int awbGain = config["camera"]["awb_gain"];
    s->set_awb_gain(s, awbGain);
    logDebug("\tAWB Gain: %d", awbGain);
  }

  if (!config["camera"]["wb_mode"].isNull()) {
    int wbMode = config["camera"]["wb_mode"];
    s->set_wb_mode(s, wbMode);
    logDebug("\tWB Mode: %d", wbMode);
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
    logDebug("\tRotation: %d°", rotation);
  }
}

void ESPWiFi::deinitCamera() {
  logInfo("📷 Deinitializing Camera");

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
    logInfo("📷 Clearing Camera Buffer");
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

  logInfo("📸 Snapshot Taken");

  bool writeSuccess = false;
  if (sdCardInitialized && sd) {
    writeSuccess = writeFile(sd, filePath, fb->buf, fb->len);
  }

  if (!writeSuccess && lfs) {
    logWarn("📁 Falling back to LittleFS for Snapshot");
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
      logInfo("📷 Disconnecting Camera WebSocket clients");
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