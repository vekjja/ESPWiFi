#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <algorithm>
#include <list>

#include "ESPWiFi.h"

class WebSocket {
private:
  ESPWiFi *espWifi = nullptr; // Store ESPWiFi instance

public:
  AsyncWebSocket *socket = nullptr;

  WebSocket() {}

  WebSocket(String path, ESPWiFi *espWifi,
            void (*onWsEvent)(AsyncWebSocket *, AsyncWebSocketClient *,
                              AwsEventType, void *, uint8_t *, size_t,
                              ESPWiFi *espWifi) = nullptr) {
    this->espWifi = espWifi; // Store the ESPWiFi instance

    // Create WebSocket with error handling
    socket = new AsyncWebSocket(path.c_str());
    if (!socket) {
      if (espWifi) {
        espWifi->logError("Failed to create WebSocket");
      }
      return;
    }

    socket->onEvent([this, onWsEvent, espWifi](AsyncWebSocket *server,
                                               AsyncWebSocketClient *client,
                                               AwsEventType type, void *arg,
                                               uint8_t *data, size_t len) {
      if (!espWifi || !client)
        return; // Early return if no ESPWiFi instance or invalid client

      if (type == WS_EVT_CONNECT) {
        espWifi->logInfo("🔌 WebSocket Client Connected: %s 🔗", socket->url());
        espWifi->logDebug("\tID: %d", client->id());
        espWifi->logDebug("\tPort: %d", client->remotePort());
        espWifi->logDebug("\tIP: %s", client->remoteIP().toString().c_str());
      } else if (type == WS_EVT_DISCONNECT) {
        espWifi->logInfo("🔌 WebSocket Client Disconnected: %s ⛓️‍💥",
                         socket->url());
        espWifi->logDebug("\tID: %d", client->id());
        espWifi->logDebug("\tDisconnect Time: %lu ms", millis());
      }

      if (onWsEvent)
        onWsEvent(server, client, type, arg, data, len, espWifi);
    });

    if (espWifi) {
      espWifi->initWebServer();
      espWifi->webServer->addHandler(socket);
      espWifi->logInfo("🔌  WebSocket Started: %s", path.c_str());
    }
  }

  ~WebSocket() {
    // Clean up the socket
    if (socket) {
      delete socket;
      socket = nullptr;
    }
  }

  operator bool() const { return socket != nullptr; }

  void textAll(const String &message) { socket->textAll(message); }

  void binaryAll(const char *data, size_t len) {
    if (data && len > 0 && socket) {
      try {
        socket->binaryAll((const uint8_t *)data, len);
      } catch (...) {
        if (espWifi) {
          espWifi->logError("WebSocket binaryAll failed");
        }
      }
    }
  }

  int numClients() {
    if (socket) {
      return socket->count();
    }
    return 0;
  }
};

#endif