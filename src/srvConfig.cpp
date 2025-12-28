#ifndef ESPWiFi_SRV_CONFIG
#define ESPWiFi_SRV_CONFIG

#include "ESPWiFi.h"
#include <sys/stat.h>

void ESPWiFi::srvConfig() {
  if (!webServer) {
    log(ERROR,
        "Cannot start config API /api/config: web server not initialized");
    return;
  }

  // Config GET endpoint
  httpd_uri_t config_get_route = {.uri = "/config",
                                  .method = HTTP_GET,
                                  .handler = [](httpd_req_t *req) -> esp_err_t {
                                    ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

                                    std::string json;
                                    serializeJson(espwifi->config, json);
                                    espwifi->sendJsonResponse(req, 200, json);
                                    return ESP_OK;
                                  },
                                  .user_ctx = this};
  httpd_register_uri_handler(webServer, &config_get_route);

  // Config PUT endpoint
  httpd_uri_t config_put_route = {
      .uri = "/config",
      .method = HTTP_PUT,
      .handler = [](httpd_req_t *req) -> esp_err_t {
        ESPWiFi *espwifi = (ESPWiFi *)req->user_ctx;

        JsonDocument reqJson = espwifi->readRequestBody(req);

        // Check if JSON document is empty (parse failed or empty input)
        if (reqJson.size() == 0) {
          espwifi->sendJsonResponse(req, 400, "{\"error\":\"EmptyInput\"}");
          espwifi->log(ERROR, "/config Error parsing JSON: EmptyInput");
          return ESP_OK;
        }

        JsonDocument mergedConfig =
            espwifi->mergeJson(espwifi->config, reqJson);

        espwifi->config = mergedConfig;

        std::string responseJson;
        serializeJson(mergedConfig, responseJson);

        // Request deferred config save (will happen in runSystem() main task)
        espwifi->requestConfigSave();
        espwifi->handleConfig();

        // Return the updated config (using the already serialized string)
        espwifi->sendJsonResponse(req, 200, responseJson);
        return ESP_OK;
      },
      .user_ctx = this};
  httpd_register_uri_handler(webServer, &config_put_route);
}

#endif // ESPWiFi_CONFIG
