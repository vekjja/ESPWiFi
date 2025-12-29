#ifndef ESPWiFi_SRV_WILDCARD
#define ESPWiFi_SRV_WILDCARD

#include "ESPWiFi.h"

void ESPWiFi::srvWildcard() {
  // GET handler for static files
  (void)registerRoute(
      "/*", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        (void)espwifi->sendFileResponse(req, req->uri, &clientInfo);
        return ESP_OK; // sendFileResponse() owns success/error responses
      });
}

#endif // ESPWiFi_SRV_WILDCARD
