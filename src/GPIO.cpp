// GPIO.cpp - Stubbed for now
#include "ESPWiFi.h"

void ESPWiFi::srvGPIO() {
  if (!webServer) {
    log(ERROR, "Cannot start GPIO: web server not initialized");
    return;
  }

  httpd_uri_t gpio_route = {.uri = "/api/gpio",
                            .method = HTTP_POST,
                            .handler = [](httpd_req_t *req) -> esp_err_t {
                              ESPWIFI_ROUTE_GUARD(req, espwifi, clientInfo);
                              espwifi->sendJsonResponse(
                                  req, 200, "{\"status\":\"ok\"}", &clientInfo);
                              return ESP_OK;
                            },
                            .user_ctx = this};
  httpd_register_uri_handler(webServer, &gpio_route);
}