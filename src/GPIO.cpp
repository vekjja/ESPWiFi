#ifndef ESPWIFI_GPIO_H
#define ESPWIFI_GPIO_H

#include <ArduinoJson.h>

#include "ESPWiFi.h"

void ESPWiFi::startGPIO() {
  webServer.on("/gpio", HTTP_OPTIONS, [this]() { handleCorsPreflight(); });
  webServer.on("/gpio", HTTP_POST, [this]() {
    String body = webServer.arg("plain");
    JsonDocument reqJson;
    DeserializationError error = deserializeJson(reqJson, body);

    if (error) {
      webServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    int pinNum = reqJson["num"];
    String mode = reqJson["mode"];
    String state = reqJson["state"];
    int duty = reqJson["duty"];

    mode.toLowerCase();
    state.toLowerCase();

    if (mode == "out" || mode == "output" || mode == "pwm") {
      pinMode(pinNum, OUTPUT);
    } else if (mode == "input" || mode == "in") {
      pinMode(pinNum, INPUT);
    } else {
      webServer.send(400, "application/json",
                     "{\"error\":\"Invalid mode\": \"" + mode + "\"}");
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
      webServer.send(400, "application/json",
                     "{\"error\":\"Invalid state\" : \"" + state + "\"}");

      return;
    }

    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "application/json", "{\"status\":\"Success\"}");
    Serial.println("GPIO " + String(pinNum) + " " + mode + " " + state + " " +
                   String(duty));
  });
  Serial.println("✅ 📍 GPIO Enabled");
}

#endif  // ESPWIFI_GPIO_H