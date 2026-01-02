#ifndef ESPWiFi_SRV_CONFIG
#define ESPWiFi_SRV_CONFIG

#include "ESPWiFi.h"
#include <sys/stat.h>

void ESPWiFi::srvConfig() {
  // Config GET endpoint
  registerRoute("/api/config", HTTP_GET,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  std::string json;
                  serializeJson(espwifi->config, json);
                  return espwifi->sendJsonResponse(req, 200, json, &clientInfo);
                });

  // Config PUT endpoint
  registerRoute("/api/config", HTTP_PUT,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  JsonDocument reqJson = espwifi->readRequestBody(req);

                  // Check if JSON document is empty (parse failed or empty
                  // input)
                  if (reqJson.size() == 0) {
                    return espwifi->sendJsonResponse(
                        req, 400, "{\"error\":\"EmptyInput\"}", &clientInfo);
                  }

                  // Merge config but don't replace yet - do that in main loop
                  JsonDocument mergedConfig =
                      espwifi->mergeJson(espwifi->config, reqJson);

                  // Store merged config for atomic replacement in main loop
                  espwifi->configUpdate = mergedConfig;

                  std::string responseJson;
                  serializeJson(mergedConfig, responseJson);

                  // Request deferred config save to main task
                  espwifi->requestConfigSave();

                  // Return the updated config (using the already serialized
                  // string)
                  return espwifi->sendJsonResponse(req, 200, responseJson,
                                                   &clientInfo);
                });
}

#endif // ESPWiFi_CONFIG
