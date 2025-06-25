#ifndef ESPWIFI_WEBSOCKET
#define ESPWIFI_WEBSOCKET

#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>

#include "ESPWiFi.h"

// WebSocket event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  const char *url = server->url();
  if (type == WS_EVT_CONNECT) {
    Serial.printf("🔗 [%s] Client #%u: %s\n", url, client->id(),
                  client->remoteIP().toString().c_str());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("⛓️‍💥  [%s] Client #%u Disconnected\n", url,
                  client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {
      String msg = String((char *)data).substring(0, len);
      Serial.printf("🔵 [%s] Client #%u: %s\n", url, client->id(), msg.c_str());
    }
  } else if (type == WS_EVT_ERROR) {
    Serial.printf("❗ [%s] Error: %s\n", url, (char *)arg);
  }
}

class WebSocket {
 private:
 public:
  AsyncWebSocket *socket;

  WebSocket(String path, ESPWiFi *espWifi,
            void (*onWsEvent)(AsyncWebSocket *, AsyncWebSocketClient *,
                              AwsEventType, void *, uint8_t *,
                              size_t) = onWsEvent) {
    socket = new AsyncWebSocket(path.c_str());
    socket->onEvent(onWsEvent);
    espWifi->initWebServer();
    espWifi->webServer->addHandler(socket);
    Serial.printf("⛓️  WebSocket Started: %s\n", path.c_str());
  };

  ~WebSocket() {
    if (socket) {
      delete socket;
      socket = nullptr;
    }
  };

  void textAll(const String &message) {
    if (socket) {
      socket->textAll(message);
    }
  }

  void binaryAll(const char *data, size_t len) {
    if (socket) {
      socket->binaryAll(data, len);
    }
  }
};

#endif  // ESPWIFI_WEBSOCKET