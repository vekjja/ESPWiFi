// MediaSocket.cpp - Media WebSocket endpoint (camera streaming on request)
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

// Auth check helper for WebSocket
static bool wsAuthCheck(httpd_req_t *req, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi || !espwifi->authEnabled() ||
      espwifi->isExcludedPath(req->uri)) {
    return true;
  }

  bool ok = espwifi->authorized(req);
  if (!ok) {
    // Browser WebSocket APIs can't set Authorization headers. Allow token
    // via query param: ws://host/path?token=...
    const std::string tok = espwifi->getQueryParam(req, "token");
    const char *expectedC = espwifi->config["auth"]["token"].as<const char *>();
    const std::string expected = (expectedC != nullptr) ? expectedC : "";
    ok = (!tok.empty() && !expected.empty() && tok == expected);
  }
  return ok;
}

static void mediaOnConnect(WebSocket *ws, int clientFd, void *userCtx) {
  (void)ws;
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }
  espwifi->log(INFO, "🎞️ LAN client connected to /ws/media (fd=%d)", clientFd);
}

static void mediaOnDisconnect(WebSocket *ws, int clientFd, void *userCtx) {
  (void)ws;
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }

  espwifi->log(INFO, "🎞️ LAN client disconnected from /ws/media (fd=%d)",
               clientFd);

#if ESPWiFi_HAS_CAMERA
  // Stop streaming for this client. Camera deinit is handled by stream loop.
  espwifi->clearMediaCameraStreamSubscribed(clientFd);
#endif
}

static void sendMediaAck(WebSocket *ws, int clientFd,
                         const JsonDocument &resp) {
  if (!ws || clientFd <= 0) {
    return;
  }
  std::string out;
  serializeJson(resp, out);
  (void)ws->sendText(clientFd, out.c_str(), out.size());
}

static void mediaOnMessage(WebSocket *ws, int clientFd, httpd_ws_type_t type,
                           const uint8_t *data, size_t len, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!ws || !espwifi || clientFd <= 0 || data == nullptr || len == 0) {
    return;
  }

  // Media control is JSON text; media payloads are sent device->client as
  // binary.
  if (type != HTTPD_WS_TYPE_TEXT) {
    return;
  }

  JsonDocument req;
  DeserializationError err = deserializeJson(req, (const char *)data, len);

  JsonDocument resp;
  resp["ok"] = true;
  resp["type"] = "media_ack";

  if (err) {
    resp["ok"] = false;
    resp["error"] = "bad_json";
    resp["detail"] = err.c_str();
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  const char *cmd = req["cmd"] | "";
  resp["cmd"] = cmd;

#if !ESPWiFi_HAS_CAMERA
  (void)espwifi;
  if (strcmp(cmd, "camera_start") == 0 || strcmp(cmd, "camera_stop") == 0 ||
      strcmp(cmd, "camera_frame") == 0) {
    resp["ok"] = false;
    resp["error"] = "camera_not_supported";
    sendMediaAck(ws, clientFd, resp);
    return;
  }
#else
  if (strcmp(cmd, "camera_start") == 0) {
    // Explicit request: start streaming to this client (pull-based "start").
    if (!espwifi->initCamera()) {
      resp["ok"] = false;
      resp["error"] = "camera_init_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }
    espwifi->setMediaCameraStreamSubscribed(clientFd, true);
    resp["streaming"] = true;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "camera_stop") == 0) {
    espwifi->setMediaCameraStreamSubscribed(clientFd, false);
    resp["streaming"] = false;
    sendMediaAck(ws, clientFd, resp);
    return;
  }

  if (strcmp(cmd, "camera_frame") == 0) {
    // One-shot frame: capture and send binary to requester only.
    if (!espwifi->initCamera()) {
      resp["ok"] = false;
      resp["error"] = "camera_init_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb || fb->format != PIXFORMAT_JPEG || !fb->buf || fb->len == 0) {
      if (fb) {
        esp_camera_fb_return(fb);
      }
      resp["ok"] = false;
      resp["error"] = "camera_capture_failed";
      sendMediaAck(ws, clientFd, resp);
      return;
    }

    // Send metadata (text) + payload (binary).
    resp["type"] = "camera_frame";
    resp["len"] = (uint32_t)fb->len;
    sendMediaAck(ws, clientFd, resp);
    (void)ws->sendBinary(clientFd, fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return;
  }
#endif

  resp["ok"] = false;
  resp["error"] = "unknown_cmd";
  sendMediaAck(ws, clientFd, resp);
}

#endif

void ESPWiFi::startMediaWebSocket() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
  if (mediaSocStarted) {
    return;
  }

  // Note: Keep unauthenticated by default to preserve current LAN UX.
  // When auth is enabled, callers can still connect using ?token=...
  mediaSocStarted = mediaSoc.begin("/ws/media", webServer, this,
                                   /*onMessage*/ mediaOnMessage,
                                   /*onConnect*/ mediaOnConnect,
                                   /*onDisconnect*/ mediaOnDisconnect,
                                   /*maxMessageLen*/ 2048,
                                   /*maxBroadcastLen*/ 200 * 1024,
                                   /*requireAuth*/ false,
                                   /*authCheck*/ wsAuthCheck);
  if (!mediaSocStarted) {
    log(ERROR, "🎞️ Media WebSocket failed to start");
    return;
  }

  log(INFO, "🎞️ Media WebSocket started: /ws/media");
#endif
}
