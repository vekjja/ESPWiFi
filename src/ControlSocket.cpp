// ControlSocket.cpp - Control WebSocket endpoint for LAN
#include "ESPWiFi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT

// For get_rssi
#include "esp_wifi.h"

// For GPIO control
#include "driver/gpio.h"
#include "driver/ledc.h"

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
    } else if (strcmp(cmd, "get_config") == 0) {
      resp["config"] = espwifi->config;
    } else if (strcmp(cmd, "get_info") == 0) {
      JsonDocument infoDoc = espwifi->buildInfoJson(false);
      resp["info"] = infoDoc.as<JsonVariantConst>();
    } else if (strcmp(cmd, "get_claim") == 0) {
      const bool rotate = req["rotate"] | false;
      resp["code"] = espwifi->getClaimCode(rotate);
      resp["expires_in_ms"] = espwifi->claimExpiresInMs();
    } else if (strcmp(cmd, "get_rssi") == 0) {
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
    } else if (strcmp(cmd, "set_gpio") == 0) {
      // Set GPIO pin: {cmd: "set_gpio", pin: 2, state: 1}
      if (!req["pin"].is<int>()) {
        resp["ok"] = false;
        resp["error"] = "missing_pin";
      } else {
        const int pin = req["pin"];
        const int state = req["state"] | 0;
        std::string errorMsg;

        if (espwifi->setGPIO(pin, state != 0, errorMsg)) {
          resp["pin"] = pin;
          resp["state"] = state ? 1 : 0;
        } else {
          resp["ok"] = false;
          resp["error"] = errorMsg;
        }
      }
    } else if (strcmp(cmd, "get_gpio") == 0) {
      // Get GPIO pin state: {cmd: "get_gpio", pin: 2}
      if (!req["pin"].is<int>()) {
        resp["ok"] = false;
        resp["error"] = "missing_pin";
      } else {
        const int pin = req["pin"];
        int state = 0;
        std::string errorMsg;

        if (espwifi->getGPIO(pin, state, errorMsg)) {
          resp["pin"] = pin;
          resp["state"] = state;
        } else {
          resp["ok"] = false;
          resp["error"] = errorMsg;
        }
      }
    } else if (strcmp(cmd, "set_pwm") == 0) {
      // Set PWM: {cmd: "set_pwm", pin: 2, duty: 128, freq: 5000}
      if (!req["pin"].is<int>()) {
        resp["ok"] = false;
        resp["error"] = "missing_pin";
      } else {
        const int pin = req["pin"];
        const int duty = req["duty"] | 0; // 0-255
        const int freq = req["freq"] | 5000;
        std::string errorMsg;

        if (espwifi->setPWM(pin, duty, freq, errorMsg)) {
          resp["pin"] = pin;
          resp["duty"] = duty;
          resp["freq"] = freq;
        } else {
          resp["ok"] = false;
          resp["error"] = errorMsg;
        }
      }
    } else {
      resp["ok"] = false;
      resp["error"] = "unknown_cmd";
    }
  }

  std::string out;
  serializeJson(resp, out);

  // Send response back to client
  // Broadcast RSSI for heartbeat visibility, otherwise send to requester only
  if (resp["cmd"] == "get_rssi") {
    (void)ws->broadcastText(out.c_str(), out.size());
  } else {
    (void)ws->sendText(clientFd, out.c_str(), out.size());
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
  // No cleanup needed
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

  log(INFO, "🎛️ Control WebSocket started: /ws/control");
#endif
}
