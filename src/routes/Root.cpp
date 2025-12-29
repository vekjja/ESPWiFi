#ifndef ESPWiFi_SRV_ROOT
#define ESPWiFi_SRV_ROOT

#include "ESPWiFi.h"

void ESPWiFi::srvRoot() {
  // Root route - serve index.html from LFS (no auth required)
  (void)registerRoute(
      "/", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        (void)espwifi->sendFileResponse(req, "/index.html", &clientInfo);
        return ESP_OK; // sendFileResponse() owns success/error responses
      });
}

#endif // ESPWiFi_SRV_ROOT
