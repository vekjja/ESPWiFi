// GPIO.cpp - Stubbed for now
#include "ESPWiFi.h"

void ESPWiFi::srvGPIO() {
  if (!webServer) {
    log(ERROR, "Cannot start GPIO: web server not initialized");
    return;
  }

  HTTPRoute("/api/gpio", HTTP_POST, [](httpd_req_t *req) {
    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;
    if (espwifi == nullptr) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    espwifi->sendJsonResponse(req, 200, "{\"status\":\"ok\"}");
    return ESP_OK;
  });
}