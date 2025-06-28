#ifndef CAMERA_H
#define CAMERA_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#ifdef ESP32
#include "esp_camera.h"
#include <WebSocket.h>

class Camera {
public:
  Camera();
  ~Camera();

  bool begin();
  bool takeSnapshot(String filePath = "/snapshot.jpg");
  bool streamCamera(int frameRate = 10);
  void setupWebServer(AsyncWebServer *server, JsonDocument &config);
  void setWebSocket(WebSocket *ws);
  bool isInitialized() const { return initialized; }

private:
  bool initialized;
  WebSocket *camSoc;
  AsyncWebServer *webServer;
  JsonDocument *config;

  void addCORS(AsyncWebServerResponse *response);
  void handleSnapshotRequest(AsyncWebServerRequest *request);
  void handleLiveStreamRequest(AsyncWebServerRequest *request);
  void handleMJPEGStreamRequest(AsyncWebServerRequest *request);
  void handleWebSocketStreamRequest(AsyncWebServerRequest *request);
};

#else
// Stub class for non-ESP32 platforms
class Camera {
public:
  Camera() {}
  ~Camera() {}

  bool begin() {
    log("⚠️ Camera support is only available on ESP32 devices.");
    return false;
  }
  bool takeSnapshot(String filePath = "/snapshot.jpg") {
    log("⚠️ Camera support is only available on ESP32 devices.");
    return false;
  }
  bool streamCamera(int frameRate = 10) {
    log("⚠️ Camera support is only available on ESP32 devices.");
    return false;
  }
  void setupWebServer(AsyncWebServer *server, JsonDocument &config) {
    log("⚠️ Camera support is only available on ESP32 devices.");
  }
  void setWebSocket(void *ws) {
    log("⚠️ Camera support is only available on ESP32 devices.");
  }
  bool isInitialized() const { return false; }
};

#endif

#endif // CAMERA_H