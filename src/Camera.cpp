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

WebSocket *camSoc = nullptr;
String camSocPath = "/camera";

void cameraWebSocketEventHandler(AsyncWebSocket *server,
                                 AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data,
                                 size_t len, ESPWiFi *espWifi) {
  if (type == WS_EVT_DATA) {
    String receivedData = String((char *)data, len);
    receivedData.trim();  // Remove any whitespace
    espWifi->log("🔌 WebSocket Data Received: 📨");
    espWifi->logf("\tClient ID: %d\n", client->id());
    espWifi->logf("\tData Length: %d bytes\n", len);
    espWifi->logf("\tData: %s\n", receivedData.c_str());
  }
}

camera_config_t ESPWiFi::getCamConfig() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming

  // ESP32-CAM optimized settings
  if (psramFound()) {
    // With PSRAM - can use higher resolution
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 20;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Without PSRAM - use smaller resolution to avoid memory issues
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 30;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    String errMsg = String(err);
    logError("Camera Init Failed: " + errMsg + "\n");
    return camera_config_t();
  }

  return config;
}

void ESPWiFi::startCamera() {
  camera_config_t camConfig = getCamConfig();

  initWebServer();
  webServer->on(
      "/camera/snapshot", HTTP_GET, [this](AsyncWebServerRequest *request) {
        takeSnapshot();
        AsyncWebServerResponse *response =
            request->beginResponse(LittleFS, "/snapshot.jpg", "image/jpeg");
        response->addHeader("Content-Disposition",
                            "inline; filename=snapshot.jpg");
        addCORS(response);
        request->send(response);
      });

  webServer->on(
      "/camera/stream/live", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String deviceName = config["mdns"];
        String html =
            "<!DOCTYPE html><html><head><title>" + deviceName +
            " Stream</title>"
            "<style>body{background:#181a1b;color:#e8eaed;font-family:sans-"
            "serif;"
            "text-align:center;}"
            "img{max-width:100vw;max-height:90vh;border-radius:8px;box-shadow:"
            "0 "
            "2px 8px #0008;margin-top:2em;}</style>"
            "</head><body>"
            "<h2>&#128247; " +
            deviceName +
            " Live Stream</h2>"
            "<img src='/camera/stream/mjpeg' id='stream' alt='Camera Stream'>"
            "</body></html>";
        request->send(200, "text/html", html);
      });

  webServer->on(
      "/camera/stream/mjpeg", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "multipart/x-mixed-replace; boundary=frame",
            [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
              // Static variables to keep state between calls
              static camera_fb_t *fb = nullptr;
              static size_t sent = 0;
              static String head;
              static String tail = "\r\n";
              static size_t totalLen = 0;

              if (fb == nullptr) {
                fb = esp_camera_fb_get();
                if (!fb) return 0;
                head = "--frame\r\nContent-Type: image/jpeg\r\n\r\n";
                sent = 0;
                totalLen = head.length() + fb->len + tail.length();
              }

              size_t toSend = totalLen - sent;
              if (toSend > maxLen) toSend = maxLen;

              size_t bufPos = 0;
              size_t remain = toSend;

              // Send head
              if (sent < head.length()) {
                size_t headRemain = head.length() - sent;
                size_t copyLen = (remain < headRemain) ? remain : headRemain;
                memcpy(buffer, head.c_str() + sent, copyLen);
                bufPos += copyLen;
                sent += copyLen;
                remain -= copyLen;
                if (remain == 0) return bufPos;
              }

              // Send JPEG
              size_t jpegStart =
                  (sent > head.length()) ? sent - head.length() : 0;
              if (sent >= head.length() && sent < head.length() + fb->len) {
                size_t jpegRemain = fb->len - jpegStart;
                size_t copyLen = (remain < jpegRemain) ? remain : jpegRemain;
                memcpy(buffer + bufPos, fb->buf + jpegStart, copyLen);
                bufPos += copyLen;
                sent += copyLen;
                remain -= copyLen;
                if (remain == 0) return bufPos;
              }

              // Send tail
              size_t tailStart = (sent > head.length() + fb->len)
                                     ? sent - head.length() - fb->len
                                     : 0;
              if (sent >= head.length() + fb->len && sent < totalLen) {
                size_t tailRemain = tail.length() - tailStart;
                size_t copyLen = (remain < tailRemain) ? remain : tailRemain;
                memcpy(buffer + bufPos, tail.c_str() + tailStart, copyLen);
                bufPos += copyLen;
                sent += copyLen;
                remain -= copyLen;
              }

              // If finished sending this frame, clean up and prepare for next
              if (sent >= totalLen) {
                esp_camera_fb_return(fb);
                fb = nullptr;
                sent = 0;
                delay(100);  // ~10 fps
              }
              return bufPos;
            });
        request->send(response);
      });

  webServer->on(
      "/camera/live", HTTP_GET, [this](AsyncWebServerRequest *request) {
        String deviceName = config["mdns"];
        String html =
            "<!DOCTYPE html><html><head><title>" + deviceName +
            " WebSocket Stream</title>"
            "<style>body{background:#181a1b;color:#e8eaed;font-family:sans-"
            "serif;text-align:center;}"
            "img{max-width:100vw;max-height:90vh;border-radius:8px;box-shadow:"
            "0 2px 8px #0008;margin-top:2em;}"
            "#status{margin:1em;font-size:1.1em;}"
            "</style>"
            "</head><body>"
            "<h2>&#128247; " +
            deviceName +
            " WebSocket Live Stream</h2>"
            "<div id='status'>Connecting...</div>"
            "<img id='stream' alt='Camera Stream'/>"
            "<script>\n"
            "const img = document.getElementById('stream');\n"
            "const status = document.getElementById('status');\n"
            "let ws = new WebSocket((location.protocol === 'https:' ? 'wss://' "
            ": 'ws://') + location.host + '" +
            camSoc->socket->url() +
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

  logf("📷 Camera Started\n");

  camSoc = new WebSocket(camSocPath, this, cameraWebSocketEventHandler);

  if (!camSoc) {
    logError(" Failed to create Camera WebSocket");
    return;
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

  // save the image LittleFS
  File file = LittleFS.open(filePath, "w");
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
  if (!camSoc) return;  // Ensure WebSocket and clients exist

  unsigned long interval =
      frameRate > 0 ? (1000 / frameRate)
                    : 500;  // Default to 500ms if frameRate is 0 or negative

  static IntervalTimer timer(1000);
  if (!timer.shouldRun(interval)) {
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    logError("Camera Capture Failed");
    return;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    logError("Unsupported Pixel Format");
    esp_camera_fb_return(fb);
    return;
  }

  // Validate buffer before sending
  if (fb->buf && fb->len > 0) {
    camSoc->binaryAll((const char *)fb->buf, fb->len);
  } else {
    logError("Invalid camera buffer");
  }

  esp_camera_fb_return(fb);
  delay(200);  // Add delay to prevent queue overflow
}

#endif  // ESPWiFi_CAMERA
#endif  // ESPWiFi_CAMERA_ENABLED