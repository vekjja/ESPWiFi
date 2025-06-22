#ifndef ESPWIFI_DEAUTH_H
#define ESPWIFI_DEAUTH_H

#include "ESPWiFi.h"

void ESPWiFi::enableDeAuth() {
  webServer.on("/deauth", HTTP_OPTIONS, [this]() { handleCorsPreflight(); });
  webServer.on("/deauth", HTTP_POST, [this]() {
    String body = webServer.arg("plain");
    JsonDocument reqJson;
    DeserializationError error = deserializeJson(reqJson, body);

    if (error) {
      webServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    String ssid = reqJson["ssid"];
    String bssid = reqJson["bssid"];

    if (ssid.isEmpty() && bssid.isEmpty()) {
      webServer.send(400, "application/json",
                     "{\"error\":\"SSID or BSSID must be provided\"}");
      return;
    }

    // Deauthentication logic here
    // This is a placeholder for actual deauth logic
    Serial.println("Deauth request received for SSID: " + ssid +
                   ", BSSID: " + bssid);

    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "application/json", "{\"status\":\"Success\"}");
  });
}

#endif  // ESPWIFI_DEAUTH_H