#ifndef ESPWiFi_SRV_WILDCARD
#define ESPWiFi_SRV_WILDCARD

#include "ESPWiFi.h"

void ESPWiFi::srvWildcard() {
  // GET handler for static files
  (void)registerRoute(
      "/*", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        // req->uri can include "?query"; file lookup must use only the path.
        std::string path =
            (req != nullptr) ? std::string(req->uri) : std::string("/");
        size_t q = path.find('?');
        if (q != std::string::npos) {
          path.resize(q);
        }
        // URL-decode the path to handle special characters like spaces
        path = espwifi->urlDecode(path);
        (void)espwifi->sendFileResponse(req, path, &clientInfo);
        return ESP_OK; // sendFileResponse() owns success/error responses
      });
}

#endif // ESPWiFi_SRV_WILDCARD
