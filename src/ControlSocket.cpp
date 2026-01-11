// ControlSocket.cpp - Control WebSocket endpoint for cloud/pairing
#include "ESPWiFi.h"
#include <CloudTunnel.h>

#ifdef CONFIG_HTTPD_WS_SUPPORT

// For get_rssi
#include "esp_wifi.h"

// Cloud tunnel instance for /ws/control endpoint
static CloudTunnel *s_controlCloudTunnel = nullptr;

static void ctrlOnMessage(WebSocket *ws, int clientFd, httpd_ws_type_t type,
                          const uint8_t *data, size_t len, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!ws || !espwifi || !data || len == 0) {
    return;
  }
  if (type != HTTPD_WS_TYPE_TEXT) {
    // Keep control socket simple: accept text JSON only.
    return;
  }

  JsonDocument req;
  DeserializationError err = deserializeJson(req, (const char *)data, len);
  JsonDocument resp;

  if (err) {
    resp["ok"] = false;
    resp["error"] = "bad_json";
    resp["detail"] = err.c_str();
  } else {
    const char *cmd = req["cmd"] | "";
    resp["ok"] = true;
    resp["cmd"] = cmd;

    if (strcmp(cmd, "ping") == 0) {
      resp["type"] = "pong";
    } else if (strcmp(cmd, "get_status") == 0) {
      resp["ip"] = espwifi->ipAddress();
      resp["hostname"] = espwifi->config["hostname"].as<std::string>();
      resp["wifiMode"] = espwifi->config["wifi"]["mode"].as<std::string>();
      resp["cloudTunnelEnabled"] =
          espwifi->config["cloudTunnel"]["enabled"].as<bool>();
    } else if (strcmp(cmd, "get_config") == 0) {
      // Return full config over the tunnel so the dashboard can operate in
      // paired/cloud mode without making mixed-content HTTP calls.
      resp["config"] = espwifi->config;
    } else if (strcmp(cmd, "get_info") == 0) {
      JsonDocument infoDoc = espwifi->buildInfoJson(false);
      resp["info"] = infoDoc.as<JsonVariantConst>();
    } else if (strcmp(cmd, "get_claim") == 0) {
      const bool rotate = req["rotate"] | false;
      resp["code"] = espwifi->getClaimCode(rotate);
      resp["expires_in_ms"] = espwifi->claimExpiresInMs();
    } else if (strcmp(cmd, "get_rssi") == 0) {
      // Return current RSSI (and SSID if available) as a simple control cmd.
      wifi_ap_record_t ap_info;
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        char ssid_str[33];
        memcpy(ssid_str, ap_info.ssid, 32);
        ssid_str[32] = '\0';
        resp["connected"] = true;
        resp["ssid"] = std::string(ssid_str);
        resp["rssi"] = ap_info.rssi;
      } else {
        resp["connected"] = false;
        resp["rssi"] = 0;
      }
    } else if (strcmp(cmd, "get_logs") == 0) {
      int64_t offset =
          req["offset"].isNull() ? -1 : req["offset"].as<int64_t>();
      int tailBytes =
          req["tailBytes"].isNull() ? (64 * 1024) : req["tailBytes"].as<int>();
      int maxBytes =
          req["maxBytes"].isNull() ? (8 * 1024) : req["maxBytes"].as<int>();

      bool useSD = false, useLFS = false;
      if (!espwifi->getLogFilesystem(useSD, useLFS)) {
        resp["ok"] = false;
        resp["error"] = "fs_unavailable";
      } else {
        const std::string &base =
            useSD ? espwifi->sdMountPoint : espwifi->lfsMountPoint;
        const std::string source = useSD ? "sd" : "lfs";
        const std::string virtualPath = espwifi->logFilePath;
        const std::string fullPath = base + espwifi->logFilePath;
        espwifi->fillChunkedDataResponse(resp, fullPath, virtualPath, source,
                                         offset, tailBytes, maxBytes);

        if (resp["ok"] == false && resp["error"] == "file_not_found") {
          resp["error"] = "log_not_found";
        }
        if (!resp["data"].isNull()) {
          resp["logs"] = resp["data"];
          resp.remove("data");
        }
      }
    } else if (strcmp(cmd, "camera_subscribe") == 0 ||
               strcmp(cmd, "camera_snapshot") == 0) {
      // Camera commands for cloud tunnel only (LAN uses /ws/camera)
#if ESPWiFi_HAS_CAMERA
      if (clientFd != CloudTunnel::kCloudClientFd) {
        resp["ok"] = false;
        resp["error"] = "use_ws_camera_for_lan";
      } else if (strcmp(cmd, "camera_subscribe") == 0) {
        const bool enable =
            req["enable"].isNull() ? true : req["enable"].as<bool>();
        espwifi->setCameraStreamSubscribed(clientFd, enable);
        resp["enabled"] = enable;
      } else {
        // camera_snapshot
        espwifi->requestCameraSnapshot(clientFd);
        resp["queued"] = true;
      }
#else
      resp["ok"] = false;
      resp["error"] = "camera_not_available";
#endif
    } else if (strcmp(cmd, "set_config") == 0) {
      // Merge and apply config updates on the main loop.
      if (!req["config"].is<JsonObject>() && !req["config"].is<JsonArray>()) {
        resp["ok"] = false;
        resp["error"] = "missing_config";
      } else {
        const bool queued = espwifi->queueConfigUpdate(req["config"]);
        resp["queued"] = queued;
        if (!queued) {
          resp["ok"] = false;
          resp["error"] = "queue_failed";
        }
      }
    } else {
      resp["ok"] = false;
      resp["error"] = "unknown_cmd";
    }
  }

  std::string out;
  serializeJson(resp, out);

  // Send response to the appropriate destination
  if (clientFd == CloudTunnel::kCloudClientFd) {
    // Cloud request - send back via cloud tunnel
    if (s_controlCloudTunnel && s_controlCloudTunnel->connected()) {
      s_controlCloudTunnel->sendText(out.c_str(), out.size());
    }
  } else {
    // LAN request - send back via WebSocket
    // Default: reply only to the requesting client (prevents cross-UI leakage).
    // Exception: `get_rssi` is intentionally broadcast so a CLI `wscat` session
    // can see it as a simple heartbeat while the UI is polling RSSI.
    if (resp["cmd"] == "get_rssi") {
      (void)ws->broadcastText(out.c_str(), out.size());
    } else {
      (void)ws->sendText(clientFd, out.c_str(), out.size());
    }
  }
}

static void ctrlOnConnect(WebSocket *ws, int clientFd, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!ws || !espwifi)
    return;
  JsonDocument hello;
  hello["type"] = "hello";
  hello["ok"] = true;
  hello["hostname"] = espwifi->config["hostname"].as<std::string>();
  std::string out;
  serializeJson(hello, out);
  (void)ws->sendText(clientFd, out.c_str(), out.size());
}

static void ctrlOnDisconnect(WebSocket *ws, int clientFd, void *userCtx) {
  (void)ws;
  (void)clientFd;
  (void)userCtx;
  // No cleanup needed - camera has its own socket
}

// Cloud tunnel callbacks - simple forwarding
static void cloudOnMessage(httpd_ws_type_t type, const uint8_t *data,
                           size_t len, void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }
  ctrlOnMessage(&espwifi->ctrlSoc, CloudTunnel::kCloudClientFd, type, data, len,
                userCtx);
}

static void cloudOnConnect(void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }
  espwifi->log(INFO, "☁️ Control tunnel connected");
  ctrlOnConnect(&espwifi->ctrlSoc, CloudTunnel::kCloudClientFd, userCtx);
}

static void cloudOnDisconnect(void *userCtx) {
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(userCtx);
  if (!espwifi) {
    return;
  }
  espwifi->log(INFO, "☁️ Control tunnel disconnected");
  ctrlOnDisconnect(&espwifi->ctrlSoc, CloudTunnel::kCloudClientFd, userCtx);
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
    // Browser WebSocket APIs can't set Authorization headers. Allow token
    // via query param: ws://host/path?token=...
    const std::string tok = espwifi->getQueryParam(req, "token");
    const char *expectedC = espwifi->config["auth"]["token"].as<const char *>();
    const std::string expected = (expectedC != nullptr) ? expectedC : "";
    ok = (!tok.empty() && !expected.empty() && tok == expected);
  }
  return ok;
}

#endif

void ESPWiFi::startControlWebSocket() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  log(WARNING,
      "🎛️ Control WebSocket disabled (CONFIG_HTTPD_WS_SUPPORT is off)");
  return;
#else
  if (ctrlSocStarted)
    return;

  ctrlSocStarted = ctrlSoc.begin("/ws/control", webServer, this,
                                 /*onMessage*/ ctrlOnMessage,
                                 /*onConnect*/ ctrlOnConnect,
                                 /*onDisconnect*/ ctrlOnDisconnect,
                                 /*maxMessageLen*/ 2048,
                                 /*maxBroadcastLen*/ 160 * 1024,
                                 /*requireAuth*/ false,
                                 /*authCheck*/ wsAuthCheck);
  if (!ctrlSocStarted) {
    log(ERROR, "🎛️ Control WebSocket failed to start");
    return;
  }

  // Setup cloud tunnel if configured
  const bool enabled = config["cloudTunnel"]["enabled"].isNull()
                           ? false
                           : config["cloudTunnel"]["enabled"].as<bool>();
  const char *baseUrl = config["cloudTunnel"]["baseUrl"].as<const char *>();
  const char *tokenC = config["auth"]["token"].as<const char *>();
  std::string token = (tokenC != nullptr) ? std::string(tokenC) : "";

  // Generate token if needed for cloud tunnel
  if (enabled && token.empty()) {
    token = generateToken();
    config["auth"]["token"] = token;
    requestConfigSave();
    log(INFO, "☁️ Generated auth token for cloud tunnel");
  }

  // Single-tunnel architecture: ONLY /ws/control is cloud-tunneled
  const bool shouldTunnel = enabled;

  if (shouldTunnel && baseUrl != nullptr && baseUrl[0] != '\0') {
    if (s_controlCloudTunnel == nullptr) {
      s_controlCloudTunnel = new CloudTunnel();
    }

    const std::string hostname = getHostname();
    s_controlCloudTunnel->configure(baseUrl, hostname.c_str(), token.c_str(),
                                    "ws_control");
    s_controlCloudTunnel->setCallbacks(cloudOnMessage, cloudOnConnect,
                                       cloudOnDisconnect, this);
    s_controlCloudTunnel->setEnabled(true);

    log(INFO, "☁️ Control tunnel configured: %s", baseUrl);
  }
#endif
}

// Helper to send binary data to cloud tunnel
bool ESPWiFi::sendToCloudTunnel(const uint8_t *data, size_t len) {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  (void)data;
  (void)len;
  return false;
#else
  if (s_controlCloudTunnel && s_controlCloudTunnel->connected()) {
    return s_controlCloudTunnel->sendBinary(data, len) == ESP_OK;
  }
  return false;
#endif
}

// Cloud tunnel status queries
bool ESPWiFi::cloudTunnelEnabled() const {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return false;
#else
  return s_controlCloudTunnel && s_controlCloudTunnel->enabled();
#endif
}

bool ESPWiFi::cloudTunnelConnected() const {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return false;
#else
  return s_controlCloudTunnel && s_controlCloudTunnel->connected();
#endif
}

bool ESPWiFi::cloudUIConnected() const {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return false;
#else
  return s_controlCloudTunnel && s_controlCloudTunnel->uiConnected();
#endif
}

const char *ESPWiFi::cloudUIWSURL() const {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return "";
#else
  return s_controlCloudTunnel ? s_controlCloudTunnel->uiWSURL() : "";
#endif
}

const char *ESPWiFi::cloudDeviceWSURL() const {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return "";
#else
  return s_controlCloudTunnel ? s_controlCloudTunnel->deviceWSURL() : "";
#endif
}

uint32_t ESPWiFi::cloudRegisteredAtMs() const {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return 0;
#else
  return s_controlCloudTunnel ? s_controlCloudTunnel->registeredAtMs() : 0;
#endif
}

void ESPWiFi::syncCloudTunnelFromConfig() {
#ifdef CONFIG_HTTPD_WS_SUPPORT
  if (!ctrlSocStarted || !s_controlCloudTunnel) {
    return;
  }

  const bool enabled = config["cloudTunnel"]["enabled"].isNull()
                           ? false
                           : config["cloudTunnel"]["enabled"].as<bool>();
  const char *baseUrl = config["cloudTunnel"]["baseUrl"].as<const char *>();
  const char *tokenC = config["auth"]["token"].as<const char *>();
  std::string token = (tokenC != nullptr) ? std::string(tokenC) : "";

  if (enabled && token.empty()) {
    token = generateToken();
    config["auth"]["token"] = token;
    requestConfigSave();
    log(INFO, "☁️ Generated auth token for cloud tunnel");
  }

  if (enabled && baseUrl != nullptr && baseUrl[0] != '\0') {
    const std::string hostname = getHostname();
    s_controlCloudTunnel->configure(baseUrl, hostname.c_str(), token.c_str(),
                                    "ws_control");
    s_controlCloudTunnel->setEnabled(true);
  } else {
    s_controlCloudTunnel->setEnabled(false);
  }
#endif
}
