#ifndef ESPWIFI_WEBSOCKET_H
#define ESPWIFI_WEBSOCKET_H

#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>

#include "ESPWiFi.h"

// WebSocket event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected from %s\n", client->id(),
                  client->remoteIP().toString().c_str());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {
      String msg = String((char *)data).substring(0, len);
      Serial.printf("[WS] Received: %s\n", msg.c_str());
      // Echo back
      client->text(msg);
    }
  }
}

// Call this in your setup()
void ESPWiFi::startWebSocket() {
  ws.onEvent(onWsEvent);
  webServer.addHandler(&ws);
  webServer.begin();
  Serial.println("✅ ⛓️ WebSocket Server Started on /ws");
}

// Send a message to all connected clients
void ESPWiFi::sendWebSocketMessage(const String &message) {
  ws.textAll(message);
  Serial.println("[WS] Sent: " + message);
}

#endif  // ESPWIFI_WEBSOCKET_H