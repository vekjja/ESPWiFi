// srvBLE.cpp - BLE provisioning API endpoints
#ifndef ESPWiFi_SRV_BLE
#define ESPWiFi_SRV_BLE
#include "ESPWiFi.h"

/**
 * @brief Register BLE provisioning API routes
 *
 * Provides HTTP endpoints for BLE provisioning management:
 * - GET  /api/ble/status - Get current BLE status and address
 * - POST /api/ble/start  - Start BLE provisioning
 * - POST /api/ble/stop   - Stop BLE provisioning
 *
 * These endpoints allow web-based control of BLE provisioning
 * as an alternative to automatic WiFi failure detection.
 */
void ESPWiFi::srvBLE() {
#ifdef CONFIG_BT_NIMBLE_ENABLED

  // GET /api/ble/status - Get BLE status and address
  registerRoute("/api/ble/status", HTTP_GET,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  JsonDocument doc;
                  uint8_t status = espwifi->getBLEStatus();

                  // Status codes: 0=not init, 1=init, 2=advertising,
                  // 3=connected
                  const char *statusStr[] = {"not_initialized", "initialized",
                                             "advertising", "connected"};
                  doc["status"] = statusStr[status < 4 ? status : 0];
                  doc["address"] = espwifi->getBLEAddress();
                  doc["enabled"] = espwifi->config["ble"]["enabled"].as<bool>();

                  std::string response;
                  serializeJson(doc, response);
                  return espwifi->sendJsonResponse(req, 200, response,
                                                   &clientInfo);
                });

  // POST /api/ble/start - Start BLE provisioning
  registerRoute("/api/ble/start", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  bool success = espwifi->startBLE();
                  JsonDocument doc;
                  doc["success"] = success;
                  if (success) {
                    doc["message"] = "BLE provisioning started";
                    doc["status"] = "advertising";
                  } else {
                    doc["message"] = "Failed to start BLE provisioning";
                  }

                  std::string response;
                  serializeJson(doc, response);
                  return espwifi->sendJsonResponse(req, success ? 200 : 500,
                                                   response, &clientInfo);
                });

  // POST /api/ble/stop - Stop BLE provisioning
  registerRoute("/api/ble/stop", HTTP_POST,
                [](ESPWiFi *espwifi, httpd_req_t *req,
                   const std::string &clientInfo) -> esp_err_t {
                  espwifi->deinitBLE();
                  JsonDocument doc;
                  doc["success"] = true;
                  doc["message"] = "BLE provisioning stopped";
                  doc["status"] = "stopped";

                  std::string response;
                  serializeJson(doc, response);
                  return espwifi->sendJsonResponse(req, 200, response,
                                                   &clientInfo);
                });

#endif // CONFIG_BT_NIMBLE_ENABLED
}

#endif // ESPWiFi_SRV_BLE
