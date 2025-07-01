#ifdef ESPWiFi_CAMERA_ENABLED

#ifndef ESPWIFI_CAMERA
#define ESPWIFI_CAMERA

#include <WebSocket.h>

#include "ESPWiFi.h"
#include "base64.h"
#include "esp_camera.h"

#define CAM_LED_PIN 4

WebSocket *camSoc = nullptr;
String camSocPath = "/camera";

void cameraWebSocketEventHandler(AsyncWebSocket *server,
                                 AsyncWebSocketClient *client,
                                 AwsEventType type, void *arg, uint8_t *data,
                                 size_t len, ESPWiFi *espWifi) {
  if (type == WS_EVT_DATA) {
    espWifi->log("🔌 WebSocket Data Received: 📨");
    espWifi->logf("\tClient ID: %d\n", client->id());
    espWifi->logf("\tData Length: %d bytes\n", len);
    espWifi->logf("\tData: %s\n", String((char *)data, len).c_str());

    // Convert received data to string
    String receivedData = String((char *)data, len);
    receivedData.trim();  // Remove any whitespace

    // Check if the text matches "light:on"
    if (receivedData == "light:on") {
      espWifi->log("📸 Light: ON");
      pinMode(CAM_LED_PIN, OUTPUT);
      digitalWrite(CAM_LED_PIN, HIGH);
    }
    if (receivedData == "light:off") {
      espWifi->log("📷 Light: OFF");
      pinMode(CAM_LED_PIN, OUTPUT);
      digitalWrite(CAM_LED_PIN, LOW);
    }
  }
}

void ESPWiFi::startCamera() {
  camera_config_t camConfig;
  camConfig.ledc_channel = LEDC_CHANNEL_0;
  camConfig.ledc_timer = LEDC_TIMER_0;
  camConfig.pin_d0 = 5;
  camConfig.pin_d1 = 18;
  camConfig.pin_d2 = 19;
  camConfig.pin_d3 = 21;
  camConfig.pin_d4 = 36;
  camConfig.pin_d5 = 39;
  camConfig.pin_d6 = 34;
  camConfig.pin_d7 = 35;
  camConfig.pin_xclk = 0;
  camConfig.pin_pclk = 22;
  camConfig.pin_vsync = 25;
  camConfig.pin_href = 23;
  camConfig.pin_sccb_sda = 26;
  camConfig.pin_sccb_scl = 27;
  camConfig.pin_pwdn = 32;
  camConfig.pin_reset = -1;
  camConfig.xclk_freq_hz = 20000000;
  camConfig.pixel_format = PIXFORMAT_JPEG;
  camConfig.frame_size = FRAMESIZE_SVGA;  // SVGA (800x600) is a good balance
                                          // between quality and performance
  camConfig.jpeg_quality = 30;
  camConfig.fb_count = 2;

  esp_err_t err = esp_camera_init(&camConfig);
  if (err != ESP_OK) {
    logf("❤️‍🩹 Camera Init Failed: Error 0x%x\n", err);
    return;
  }

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
      "/camera/stream/live", HTTP_GET,
      [this, &camConfig](AsyncWebServerRequest *request) {
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

#endif  // ESPWIFI_CAMERA
#endif  // ESPWiFi_CAMERA_ENABLED