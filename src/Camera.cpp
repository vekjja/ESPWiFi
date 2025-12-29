// Camera.cpp
#ifdef ESPWiFi_CAMERA
#include "ESPWiFi.h"
#include "IntervalTimer.h"
#include "WebSocket.h"

#include <CameraPins.h>
#include <esp_camera.h>

// -----------------------------------------------------------------------------
// Camera runtime (kept file-static to avoid polluting ESPWiFi.h with camera
// types)
// -----------------------------------------------------------------------------
static WebSocket s_camWs;
static bool s_camWsStarted = false;
static bool s_cameraInitialized = false;
static SemaphoreHandle_t s_camMutex = nullptr;

static bool takeCamMutex_(TickType_t ticks = pdMS_TO_TICKS(60)) {
  if (s_camMutex == nullptr) {
    s_camMutex = xSemaphoreCreateMutex();
  }
  if (s_camMutex == nullptr) {
    return false;
  }
  return xSemaphoreTake(s_camMutex, ticks) == pdTRUE;
}

static void giveCamMutex_() {
  if (s_camMutex != nullptr) {
    xSemaphoreGive(s_camMutex);
  }
}

bool ESPWiFi::initCamera() {
  if (s_cameraInitialized) {
    return true;
  }

  // Avoid re-entrancy (camera init can be called from config handler and
  // route).
  static bool initInProgress = false;
  if (initInProgress) {
    return false;
  }
  initInProgress = true;

  log(INFO, "📷 Initializing Camera");

  if (!takeCamMutex_(pdMS_TO_TICKS(200))) {
    initInProgress = false;
    return false;
  }

  // If already initialized in the driver, treat as ready.
  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {
    s_cameraInitialized = true;
    giveCamMutex_();
    initInProgress = false;
    return true;
  }

  camera_config_t cam = {};
  cam.ledc_channel = LEDC_CHANNEL_0;
  cam.ledc_timer = LEDC_TIMER_0;

  // CameraPins.h provides these based on CAMERA_MODEL_* build flag.
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

  // Default to a conservative XCLK; many sensors tolerate 20MHz.
  cam.xclk_freq_hz = 20000000;
  cam.pixel_format = PIXFORMAT_JPEG;

  // Prefer PSRAM if present for bigger frames / more buffers.
  const bool usingPSRAM = psramFound();
  if (usingPSRAM) {
    cam.frame_size = FRAMESIZE_SVGA;
    cam.jpeg_quality = 15;
    cam.fb_count = 2;
    cam.fb_location = CAMERA_FB_IN_PSRAM;
    cam.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    cam.frame_size = FRAMESIZE_QVGA;
    cam.jpeg_quality = 25;
    cam.fb_count = 1;
    cam.fb_location = CAMERA_FB_IN_DRAM;
    cam.grab_mode = CAMERA_GRAB_LATEST;
  }

  // Apply frameRate hint (keeps streaming sane). This is a soft limit; the
  // stream loop also rate-limits.
  int frameRate = config["camera"]["frameRate"].isNull()
                      ? 10
                      : config["camera"]["frameRate"].as<int>();
  if (frameRate < 1)
    frameRate = 1;
  if (frameRate > 15)
    frameRate = 15;

  // Best-effort init; do not abort on failure.
  esp_err_t err = esp_camera_init(&cam);
  if (err != ESP_OK) {
    log(ERROR, "📷 Camera Init Failed: %s", esp_err_to_name(err));
    (void)esp_camera_deinit();
    giveCamMutex_();
    initInProgress = false;
    return false;
  }

  // Apply settings from config.
  updateCameraSettings();

  s_cameraInitialized = true;
  giveCamMutex_();
  initInProgress = false;

  log(INFO, "📷 Camera initialized (%s)", usingPSRAM ? "PSRAM" : "DRAM");
  log(DEBUG, "\tTarget FPS: %d", frameRate);
  return true;
}

void ESPWiFi::startCamera() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  log(WARNING, "📷 Camera WebSocket disabled (CONFIG_HTTPD_WS_SUPPORT is off)");
  return;
#else
  if (s_camWsStarted) {
    return;
  }
  // Start WS endpoint used by dashboard CameraModule (binary JPEG frames).
  // Require auth: browser will pass token via ?token=... in URL.
  s_camWsStarted = s_camWs.begin("/camera", this,
                                 /*onMessage*/ nullptr,
                                 /*onConnect*/ nullptr,
                                 /*onDisconnect*/ nullptr,
                                 /*maxMessageLen*/ 32,
                                 /*maxBroadcastLen*/ 128 * 1024,
                                 /*requireAuth*/ true);
  if (!s_camWsStarted) {
    log(ERROR, "📷 Camera WebSocket failed to start");
  }
#endif
}

void ESPWiFi::deinitCamera() {
  // Stop streaming to clients first (best-effort).
  if (s_camWsStarted) {
    s_camWs.closeAll();
    s_camWsStarted = false;
  }

  if (!takeCamMutex_(pdMS_TO_TICKS(200))) {
    return;
  }

  if (s_cameraInitialized) {
    (void)esp_camera_deinit();
    s_cameraInitialized = false;
    log(INFO, "📷 Camera deinitialized");
  }
  giveCamMutex_();
}

void ESPWiFi::clearCameraBuffer() {
  // Drain a few frames to get "latest" data after reconfig (best-effort).
  for (int i = 0; i < 2; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
    } else {
      break;
    }
    yield();
  }
}

void ESPWiFi::updateCameraSettings() {
  sensor_t *s = esp_camera_sensor_get();
  if (s == nullptr) {
    return;
  }

  // Keep setters bounded; avoid crashing on invalid config fields.
  auto getInt = [&](const char *key, int def, int minv, int maxv) -> int {
    if (config["camera"][key].isNull()) {
      return def;
    }
    int v = config["camera"][key].as<int>();
    if (v < minv)
      v = minv;
    if (v > maxv)
      v = maxv;
    return v;
  };

  s->set_brightness(s, getInt("brightness", 1, -2, 2));
  s->set_contrast(s, getInt("contrast", 1, -2, 2));
  s->set_saturation(s, getInt("saturation", 1, -2, 2));
  s->set_ae_level(s, getInt("exposure_level", 1, -2, 2));
  s->set_aec_value(s, getInt("exposure_value", 400, 0, 1200));
  s->set_agc_gain(s, getInt("agc_gain", 2, 0, 30));
  s->set_gainceiling(s, (gainceiling_t)getInt("gain_ceiling", 2, 0, 6));
  s->set_whitebal(s, getInt("white_balance", 1, 0, 1));
  s->set_awb_gain(s, getInt("awb_gain", 1, 0, 1));
  s->set_wb_mode(s, getInt("wb_mode", 0, 0, 4));
}

void ESPWiFi::cameraConfigHandler() {
  const bool enabled = config["camera"]["enabled"].as<bool>();
  const bool installed = config["camera"]["installed"].as<bool>();
  if (!installed) {
    return;
  }

  if (!enabled) {
    deinitCamera();
    return;
  }

  // Enabled: init and start WS endpoint
  (void)initCamera();
  startCamera();
}

void ESPWiFi::streamCamera() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
  if (!config["camera"]["enabled"].as<bool>()) {
    return;
  }
  if (!s_camWsStarted || s_camWs.numClients() == 0) {
    return;
  }
  if (!s_cameraInitialized) {
    if (!initCamera()) {
      return;
    }
  }

  int frameRate = config["camera"]["frameRate"].isNull()
                      ? 10
                      : config["camera"]["frameRate"].as<int>();
  if (frameRate < 1)
    frameRate = 1;
  if (frameRate > 15)
    frameRate = 15;
  const int intervalMs = 1000 / frameRate;

  static IntervalTimer frameTimer(1000);
  frameTimer.setIntervalMs((uint32_t)intervalMs);

  const int64_t nowUs = esp_timer_get_time();
  if (!frameTimer.shouldRunAt(nowUs)) {
    return;
  }

  if (!takeCamMutex_(pdMS_TO_TICKS(15))) {
    return;
  }
  camera_fb_t *fb = esp_camera_fb_get();
  giveCamMutex_();

  if (!fb) {
    return;
  }
  if (fb->format != PIXFORMAT_JPEG || fb->buf == nullptr || fb->len == 0) {
    esp_camera_fb_return(fb);
    return;
  }

  // Send as a single binary WS message (dashboard treats each message as a
  // JPEG).
  (void)s_camWs.binaryAll((const uint8_t *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
#endif
}

esp_err_t ESPWiFi::sendCameraSnapshot(httpd_req_t *req,
                                      const std::string &clientInfo) {
  if (req == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_cameraInitialized) {
    return sendJsonResponse(req, 503, "{\"error\":\"Camera not available\"}",
                            &clientInfo);
  }

  if (!takeCamMutex_(pdMS_TO_TICKS(200))) {
    return sendJsonResponse(req, 503, "{\"error\":\"Camera busy\"}",
                            &clientInfo);
  }
  camera_fb_t *fb = esp_camera_fb_get();
  giveCamMutex_();

  if (!fb) {
    return sendJsonResponse(req, 500, "{\"error\":\"Capture failed\"}",
                            &clientInfo);
  }
  if (fb->format != PIXFORMAT_JPEG || fb->buf == nullptr || fb->len == 0) {
    esp_camera_fb_return(fb);
    return sendJsonResponse(req, 500, "{\"error\":\"Invalid frame\"}",
                            &clientInfo);
  }

  httpd_resp_set_type(req, "image/jpeg");
  // Cache control: avoid browsers caching old frames.
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  // Chunk send to keep watchdog-safe.
  const size_t CHUNK = 8192;
  size_t sent = 0;
  esp_err_t ret = ESP_OK;
  while (sent < fb->len) {
    size_t toSend = (fb->len - sent > CHUNK) ? CHUNK : (fb->len - sent);
    ret = httpd_resp_send_chunk(req, (const char *)fb->buf + sent, toSend);
    if (ret != ESP_OK) {
      break;
    }
    sent += toSend;
    yield();
  }
  // Finalize chunked transfer
  if (ret == ESP_OK) {
    ret = httpd_resp_send_chunk(req, nullptr, 0);
  } else {
    (void)httpd_resp_send_chunk(req, nullptr, 0);
  }

  esp_camera_fb_return(fb);
  logAccess((ret == ESP_OK) ? 200 : 500, clientInfo, sent);
  return ret;
}

#endif
