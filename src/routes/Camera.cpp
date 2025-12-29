#ifdef ESPWiFi_CAMERA

#ifndef ESPWiFi_SRV_CAMERA
#define ESPWiFi_SRV_CAMERA

#include "ESPWiFi.h"

void ESPWiFi::srvCamera() {
  // GET /camera/snapshot - return a single JPEG frame
  registerRoute(
      "/camera/snapshot", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // Feature-gate
        if (!espwifi || !req) {
          return ESP_ERR_INVALID_ARG;
        }

        if (!espwifi->config["camera"]["enabled"].as<bool>()) {
          return espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"Camera disabled\"}", &clientInfo);
        }

        if (!espwifi->initCamera()) {
          return espwifi->sendJsonResponse(
              req, 503, "{\"error\":\"Camera not available\"}", &clientInfo);
        }

        return espwifi->sendCameraSnapshot(req, clientInfo);
      });
}

#endif // ESPWiFi_SRV_CAMERA

#endif // ESPWiFi_CAMERA
