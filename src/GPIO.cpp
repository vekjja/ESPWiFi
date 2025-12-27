// GPIO.cpp - Stubbed for now
#include "ESPWiFi.h"

void ESPWiFi::srvGPIO() {
  if (!webServer) {
    log(ERROR, "Cannot start GPIO: web server not initialized");
    return;
  }

  httpd_uri_t gpio_route = {
      .uri = "/api/gpio",
      .method = HTTP_POST,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
        if (espwifi == nullptr) {
          httpd_resp_send_500(req);
          return ESP_FAIL;
        }
        espwifi->sendJsonResponse(req, 200, "{\"status\":\"ok\"}");
        return ESP_OK;
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &gpio_route);
}