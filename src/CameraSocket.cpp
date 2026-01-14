// CameraSocket.cpp - Dedicated WebSocket endpoint for camera streaming
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

// Auth check helper for WebSocket
static bool wsAuthCheck(PsychicRequest *request, ESPWiFi *espwifi) {
  if (!espwifi || !espwifi->authEnabled()) {
    return true;
  }

  String uri = request->uri();
  if (espwifi->isExcludedPath(uri.c_str())) {
    return true;
  }

  bool ok = espwifi->authorized(request);
  if (!ok) {
    const std::string tok =
        espwifi->getQueryParam(request, std::string("token"));
    const char *expectedC = espwifi->config["auth"]["token"].as<const char *>();
    const std::string expected = (expectedC != nullptr) ? expectedC : "";
    ok = (!tok.empty() && !expected.empty() && tok == expected);
  }
  return ok;
}

#endif

void ESPWiFi::startCameraWebSocket() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
#if ESPWiFi_HAS_CAMERA
  if (cameraSocStarted || !webServer) {
    return;
  }

  // Create WebSocket handler using PsychicHttp
  cameraSoc = new PsychicWebSocketHandler();

  // Set handlers
  ESPWiFi *self = this;
  cameraSoc->onOpen([self](PsychicWebSocketClient *client) {
    if (self) {
      self->log(INFO, "📷 Camera client connected");
    }
  });

  cameraSoc->onClose([self](PsychicWebSocketClient *client) {
    if (self) {
      self->log(INFO, "📷 Camera client disconnected");
    }
  });

  cameraSoc->onFrame(
      [](PsychicWebSocketRequest *request, httpd_ws_frame *frame) -> esp_err_t {
        // Camera socket is binary-only, no messages expected
        (void)request;
        (void)frame;
        return ESP_OK;
      });

  // Set authentication filter
  cameraSoc->addFilter([self](PsychicRequest *request) -> bool {
    return wsAuthCheck(request, self);
  });

  // Register endpoint with PsychicHttp server
  PsychicEndpoint *endpoint = webServer->on("/ws/camera", cameraSoc);
  if (endpoint == nullptr) {
    log(ERROR, "📷 Camera WebSocket failed to register");
    delete cameraSoc;
    cameraSoc = nullptr;
    return;
  }

  cameraSocStarted = true;
  log(INFO, "📷 Camera WebSocket started: /ws/camera");
#endif
#endif
}
