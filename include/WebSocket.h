#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <algorithm>
#include <list>

#include "ESPWiFi.h"

class WebSocket {
 private:
  ESPWiFi *espWifi = nullptr;  // Store ESPWiFi instance

 public:
  AsyncWebSocket *socket = nullptr;

  WebSocket() {}

  WebSocket(String path, ESPWiFi *espWifi,
            void (*onWsEvent)(AsyncWebSocket *, AsyncWebSocketClient *,
                              AwsEventType, void *, uint8_t *, size_t,
                              ESPWiFi *espWifi) = nullptr) {
    this->espWifi = espWifi;  // Store the ESPWiFi instance

    // Create WebSocket with error handling
    socket = new AsyncWebSocket(path.c_str());
    if (!socket) {
      if (espWifi) {
        espWifi->logError(" Failed to create WebSocket");
      }
      return;
    }

    socket->onEvent([this, onWsEvent, espWifi](AsyncWebSocket *server,
                                               AsyncWebSocketClient *client,
                                               AwsEventType type, void *arg,
                                               uint8_t *data, size_t len) {
      if (!espWifi || !client)
        return;  // Early return if no ESPWiFi instance or invalid client

      if (type == WS_EVT_CONNECT) {
        espWifi->logf("🔌 WebSocket Client Connected: 🔗\n");
        espWifi->logf("\tID: %d\n", client->id());
        espWifi->logf("\tPath: %s\n", socket->url());
        espWifi->logf("\tPort: %d\n", client->remotePort());
        espWifi->logf("\tIP: %s\n", client->remoteIP().toString().c_str());
      } else if (type == WS_EVT_DISCONNECT) {
        // Safely remove client from list
        espWifi->log("🔌 WebSocket Client Disconnected: ⛓️‍💥");
        espWifi->logf("\tID: %d\n", client->id());
        espWifi->logf("\tPath: %s\n", socket->url());
        espWifi->logf("\tPort: %d\n", client->remotePort());
        espWifi->logf("\tIP: %s\n", client->remoteIP().toString().c_str());
        espWifi->logf("\tDisconnect Time: %lu ms\n", millis());
      }

      if (onWsEvent) onWsEvent(server, client, type, arg, data, len, espWifi);
    });

    if (espWifi) {
      espWifi->initWebServer();
      espWifi->webServer->addHandler(socket);
      espWifi->log("🔌 WebSocket Started:");
      espWifi->logf("\tPath: %s\n", path.c_str());
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

  void textAll(const String &message) {
    if (socket) {
      socket->textAll(message);
    }
  }

  void binaryAll(const char *data, size_t len) {
    if (socket && data && len > 0) {
      socket->binaryAll((const uint8_t *)data, len);
    }
  }
};

#endif