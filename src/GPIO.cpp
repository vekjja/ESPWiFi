#ifndef ESPWiFi_GPIO
#define ESPWiFi_GPIO

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <WebSocket.h>

#include "ESPWiFi.h"

void ESPWiFi::startGPIO() {
  initWebServer();
  // CORS preflight
  webServer->on("/gpio", HTTP_OPTIONS, [this](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(204);
    addCORS(response);
    request->send(response);
  });

  // JSON POST handler
  webServer->addHandler(new AsyncCallbackJsonWebHandler(
      "/gpio", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject reqJson = json.as<JsonObject>();
        AsyncWebServerResponse *response = nullptr;
        int pinNum = reqJson["num"] | -1;
        String mode = reqJson["mode"] | "";
        String state = reqJson["state"] | "";
        int duty = reqJson["duty"] | 0;
        bool isDelete = reqJson["delete"] | false;
        mode.toLowerCase();
        state.toLowerCase();

        String errorMsg;
        if (pinNum < 0) {
          errorMsg = "{\"error\":\"Missing pin number\"}";
          response = request->beginResponse(400, "application/json", errorMsg);
          addCORS(response);
          request->send(response);
          return;
        }

        if (mode == "out" || mode == "output" || mode == "pwm") {
          pinMode(pinNum, OUTPUT);
        } else if (mode == "input" || mode == "in") {
          pinMode(pinNum, INPUT);
        } else {
          errorMsg = String("{\"error\":\"Invalid mode: ") + mode + "\"}";
          response = request->beginResponse(400, "application/json", errorMsg);
          addCORS(response);
          request->send(response);
          return;
        }
        if (state == "high") {
          if (mode == "pwm") {
            analogWrite(pinNum, duty);
          } else {
            digitalWrite(pinNum, HIGH);
          }
        } else if (state == "low") {
          digitalWrite(pinNum, LOW);
        } else {
          errorMsg = String("{\"error\":\"Invalid state: ") + state + "\"}";
          response = request->beginResponse(400, "application/json", errorMsg);
          addCORS(response);
          request->send(response);
          return;
        }
        String okMsg = "{\"status\":\"Success\"}";
        response = request->beginResponse(200, "application/json", okMsg);
        addCORS(response);
        request->send(response);

        // Log the operation - only include duty for PWM mode
        String logMsg = "📍 GPIO " + String(pinNum) + " " + mode + " " + state;
        if (mode == "pwm") {
          logMsg += " " + String(duty);
        }
        log(logMsg);
      }));
  log("📍 GPIO Started:");
  log("\tPOST /gpio");
}

#endif  // ESPWiFi_GPIO