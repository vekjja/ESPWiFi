#ifndef ESPWiFi_SRV_CONFIG
#define ESPWiFi_SRV_CONFIG

#include "ESPWiFi.h"
#include <sys/stat.h>

void ESPWiFi::srvConfig() {
  // Config GET endpoint
  registerRoute(
      "/config", HTTP_GET,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        std::string json;
        serializeJson(espwifi->config, json);
        (void)espwifi->sendJsonResponse(req, 200, json, &clientInfo);
        return ESP_OK;
      },
      true);

  // Config PUT endpoint
  registerRoute(
      "/config", HTTP_PUT,
      [](ESPWiFi *espwifi, httpd_req_t *req,
         const std::string &clientInfo) -> esp_err_t {
        JsonDocument reqJson = espwifi->readRequestBody(req);

        // Check if JSON document is empty (parse failed or empty input)
        if (reqJson.size() == 0) {
          (void)espwifi->sendJsonResponse(
              req, 400, "{\"error\":\"EmptyInput\"}", &clientInfo);
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
        (void)espwifi->sendJsonResponse(req, 200, responseJson, &clientInfo);
        return ESP_OK;
      },
      true);
}

#endif // ESPWiFi_CONFIG
