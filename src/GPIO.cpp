// GPIO.cpp - Stubbed for now
#include "ESPWiFi.h"

void ESPWiFi::srvGPIO() {
  registerRoute(
      "/api/gpio", HTTP_POST,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        (void)espwifi->sendJsonResponse(req, 200, "{\"status\":\"ok\"}",
                                        &clientInfo);
        return ESP_OK;
      });
}