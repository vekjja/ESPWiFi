#ifndef ESPWiFi_SRV_BLUETOOTH
#define ESPWiFi_SRV_BLUETOOTH

#include "ESPWiFi.h"

void ESPWiFi::srvBluetooth() {
#ifdef CONFIG_BT_A2DP_ENABLE

  registerRoute("/api/bt/start", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  espwifi->startBluetooth();
                  return espwifi->sendJsonResponse(
                      req, 200, "{\"status\":\"Success\"}", &clientInfo);
                });

  registerRoute("/api/bt/stop", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  espwifi->stopBluetooth();
                  return espwifi->sendJsonResponse(
                      req, 200, "{\"status\":\"Success\"}", &clientInfo);
                });

  registerRoute("/api/bt/scan", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  espwifi->scanBluetooth(9);
                  return espwifi->sendJsonResponse(
                      req, 200, "{\"status\":\"Success\"}", &clientInfo);
                });
#endif // CONFIG_BT_A2DP_ENABLE
}

#endif // ESPWiFi_SRV_BLUETOOTH
