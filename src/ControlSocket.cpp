// ControlSocket.cpp - Control WebSocket endpoint for cloud/pairing
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

static void ctrlOnMessage(WebSocket *ws, int clientFd, httpd_ws_type_t type,
                          const uint8_t *data, size_t len, ESPWiFi *espwifi) {
  (void)clientFd;
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
      // Return the same payload as /api/info so the dashboard sees consistent
      // data in paired/tunnel mode (no HTTP).
      JsonDocument infoDoc = espwifi->buildInfoJson(false);
      resp["info"] = infoDoc.as<JsonVariantConst>();
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
  // Note: WebSocket helper currently broadcasts; control frames are small/low
  // rate.
  ws->textAll(out.c_str(), out.size());
}

static void ctrlOnConnect(WebSocket *ws, int clientFd, ESPWiFi *espwifi) {
  (void)clientFd;
  if (!ws || !espwifi)
    return;
  JsonDocument hello;
  hello["type"] = "hello";
  hello["ok"] = true;
  hello["hostname"] = espwifi->config["hostname"].as<std::string>();
  std::string out;
  serializeJson(hello, out);
  ws->textAll(out.c_str(), out.size());
}

static void ctrlOnDisconnect(WebSocket *ws, int clientFd, ESPWiFi *espwifi) {
  (void)ws;
  (void)clientFd;
  (void)espwifi;
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

  ctrlSocStarted = ctrlSoc.begin("/ws/control", this,
                                 /*onMessage*/ ctrlOnMessage,
                                 /*onConnect*/ ctrlOnConnect,
                                 /*onDisconnect*/ ctrlOnDisconnect,
                                 /*maxMessageLen*/ 2048,
                                 // Must be large enough to return full config
                                 // over the tunnel (get_config).
                                 /*maxBroadcastLen*/ 32 * 1024,
                                 /*requireAuth*/ false);
  if (!ctrlSocStarted) {
    log(ERROR, "🎛️ Control WebSocket failed to start");
    return;
  }

  // Apply cloud tunnel config immediately.
  ctrlSoc.syncCloudTunnelFromConfig();
#endif
}
