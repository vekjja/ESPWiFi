// ControlSocket.cpp - Control WebSocket endpoint for cloud/pairing
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

// For get_rssi
#include "esp_wifi.h"
static void ctrlOnMessage(WebSocket *ws, int clientFd, httpd_ws_type_t type,
                          const uint8_t *data, size_t len, ESPWiFi *espwifi) {
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
      // Return a chunk of the log file over control WS.
      // Params:
      // - offset: byte offset to start reading (default: tail)
      // - tailBytes: when offset is omitted, start at max(0, size-tailBytes)
      // - maxBytes: max bytes to return (capped to keep WS payload small)
      int64_t offset = -1;
      if (!req["offset"].isNull()) {
        offset = req["offset"].as<int64_t>();
      }
      int tailBytes =
          req["tailBytes"].isNull() ? (64 * 1024) : req["tailBytes"].as<int>();
      // Cloud tunnel responses are forwarded to the UI and must fit the tunnel
      // buffer (and JSON escaping can grow payload). Use a smaller default for
      // cloud.
      const bool isCloud = (clientFd == -7777);
      const int defaultMaxBytes = isCloud ? (2 * 1024) : (8 * 1024);
      int maxBytes = req["maxBytes"].isNull() ? defaultMaxBytes
                                              : req["maxBytes"].as<int>();

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

        // Back-compat with existing UI expectations.
        if (resp["ok"] == false && resp["error"] == "file_not_found") {
          resp["error"] = "log_not_found";
        }
        if (!resp["data"].isNull()) {
          resp["logs"] = resp["data"];
          resp.remove("data");
        }
      }
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
  // Default: reply only to the requesting client (prevents cross-UI leakage).
  //
  // Exception: `get_rssi` is intentionally broadcast so a CLI `wscat` session
  // can see it as a simple heartbeat while the UI is polling RSSI.
  if (resp["cmd"] == "get_rssi") {
    (void)ws->textAll(out.c_str(), out.size());
  } else {
    (void)ws->textTo(clientFd, out.c_str(), out.size());
  }
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
  (void)ws->textTo(clientFd, out.c_str(), out.size());
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
