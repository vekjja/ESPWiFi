// CameraSocket.cpp - Dedicated WebSocket endpoint for camera streaming
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

static void camOnConnect(WebSocket *ws, int clientFd, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!ws || !espwifi) {
    return;
  }

  espwifi->log(INFO, "📷 LAN client connected to /ws/camera (fd=%d)", clientFd);

#if ESPWiFi_HAS_CAMERA
  // Auto-subscribe on connect
  espwifi->setCameraStreamSubscribed(clientFd, true);
  espwifi->log(DEBUG, "📷 LAN client auto-subscribed (fd=%d)", clientFd);
#endif
}

static void camOnDisconnect(WebSocket *ws, int clientFd, void *userCtx) {
  (void)ws;
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }

  espwifi->log(INFO, "📷 LAN client disconnected from /ws/camera (fd=%d)",
               clientFd);

#if ESPWiFi_HAS_CAMERA
  espwifi->clearCameraStreamSubscribed(clientFd);
#endif
}

static void camOnMessage(WebSocket *ws, int clientFd, httpd_ws_type_t type,
                         const uint8_t *data, size_t len, void *userCtx) {
  // Camera socket is binary-only, no messages expected
  (void)ws;
  (void)clientFd;
  (void)type;
  (void)data;
  (void)len;
  (void)userCtx;
}

// Auth check helper for WebSocket
static bool wsAuthCheck(httpd_req_t *req, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi || !espwifi->authEnabled() ||
      espwifi->isExcludedPath(req->uri)) {
    return true;
  }

  bool ok = espwifi->authorized(req);
  if (!ok) {
    const std::string tok = espwifi->getQueryParam(req, "token");
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
  if (cameraSocStarted) {
    return;
  }

  cameraSocStarted = cameraSoc.begin("/ws/camera", webServer, this,
                                     /*onMessage*/ camOnMessage,
                                     /*onConnect*/ camOnConnect,
                                     /*onDisconnect*/ camOnDisconnect,
                                     /*maxMessageLen*/ 0,
                                     /*maxBroadcastLen*/ 200 * 1024,
                                     /*requireAuth*/ false,
                                     /*authCheck*/ wsAuthCheck);
  if (!cameraSocStarted) {
    log(ERROR, "📷 Camera WebSocket failed to start");
    return;
  }

  log(INFO, "📷 Camera WebSocket started: /ws/camera");
#endif
#endif
}
