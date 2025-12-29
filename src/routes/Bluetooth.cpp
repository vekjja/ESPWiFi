// Bluetooth.cpp - Bluetooth Classic (A2DP source) control routes
#ifndef ESPWiFi_SRV_BLUETOOTH
#define ESPWiFi_SRV_BLUETOOTH

#include "ESPWiFi.h"
#include <sys/stat.h>

#if defined(CONFIG_BT_ENABLED)

void ESPWiFi::srvBluetooth() {
  // POST /api/bluetooth/pairing/start?seconds=10
  registerRoute("/api/bluetooth/pairing/start", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  std::string s = espwifi->getQueryParam(req, "seconds");
                  uint32_t seconds = 10;
                  if (!s.empty()) {
                    seconds = (uint32_t)std::stoul(s);
                    if (seconds == 0)
                      seconds = 10;
                    if (seconds > 60)
                      seconds = 60;
                  }
                  espwifi->btEnterPairingMode(seconds);
                  return espwifi->sendJsonResponse(
                      req, 202, "{\"status\":\"pairing_started\"}",
                      &clientInfo);
                });

  // POST /api/bluetooth/pairing/stop
  registerRoute("/api/bluetooth/pairing/stop", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  espwifi->btStopPairingMode();
                  return espwifi->sendJsonResponse(
                      req, 202, "{\"status\":\"pairing_stopped\"}",
                      &clientInfo);
                });

  // GET /api/bluetooth/scan (returns last inquiry results)
  registerRoute("/api/bluetooth/scan", HTTP_GET,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  return espwifi->sendJsonResponse(
                      req, 200, espwifi->btScanJson(), &clientInfo);
                });

  // POST /api/bluetooth/connect {"name":"MySpeaker"}
  registerRoute(
      "/api/bluetooth/connect", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        if (req->content_len > 256) {
          return espwifi->sendJsonResponse(
              req, 413, "{\"error\":\"Request body too large\"}", &clientInfo);
        }
        JsonDocument body = espwifi->readRequestBody(req);
        if (body.size() == 0) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid JSON\"}", &clientInfo);
        }
        std::string name = body["name"].isNull()
                               ? std::string()
                               : body["name"].as<std::string>();
        if (name.empty()) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing name\"}", &clientInfo);
        }
        // Persist target name for future pairing/start calls.
        espwifi->config["bluetooth"]["audio"]["targetName"] = name;
        espwifi->requestConfigUpdate();
        espwifi->btConnect(name);
        return espwifi->sendJsonResponse(
            req, 202, "{\"status\":\"connect_requested\"}", &clientInfo);
      });

  // POST /api/bluetooth/disconnect
  registerRoute("/api/bluetooth/disconnect", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  espwifi->btDisconnect();
                  return espwifi->sendJsonResponse(
                      req, 202, "{\"status\":\"disconnect_requested\"}",
                      &clientInfo);
                });

  // POST /api/bluetooth/audio/play {"path":"/sd/music.wav"}
  registerRoute(
      "/api/bluetooth/audio/play", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        if (req->content_len > 384) {
          return espwifi->sendJsonResponse(
              req, 413, "{\"error\":\"Request body too large\"}", &clientInfo);
        }
        JsonDocument body = espwifi->readRequestBody(req);
        if (body.size() == 0) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Invalid JSON\"}", &clientInfo);
        }
        std::string path = body["path"].isNull()
                               ? std::string()
                               : body["path"].as<std::string>();
        if (path.empty()) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Missing path\"}", &clientInfo);
        }

        // Fast preflight: make sure SD is mounted and file exists.
        if (path.rfind("/sd/", 0) != 0) {
          return espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"Path must start with /sd/\"}",
              &clientInfo);
        }
        if (!espwifi->sdCardInitialized) {
          return espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"SD not mounted\"}", &clientInfo);
        }
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
          return espwifi->sendJsonResponse(
              req, 404, "{\"error\":\"File not found\"}", &clientInfo);
        }

        espwifi->btPlayWavFromSd(path);
        return espwifi->sendJsonResponse(
            req, 202, "{\"status\":\"play_requested\"}", &clientInfo);
      });

  // POST /api/bluetooth/audio/stop
  registerRoute("/api/bluetooth/audio/stop", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  espwifi->btStopAudio();
                  return espwifi->sendJsonResponse(
                      req, 202, "{\"status\":\"stop_requested\"}", &clientInfo);
                });

  // GET /api/bluetooth/status
  registerRoute("/api/bluetooth/status", HTTP_GET,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  return espwifi->sendJsonResponse(
                      req, 200, espwifi->btStatusJson(), &clientInfo);
                });
}

#endif // CONFIG_BT_ENABLED

#endif // ESPWiFi_SRV_BLUETOOTH
