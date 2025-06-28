#include "ESPWiFi.h"
#include <algorithm>
#include <list>

class WebSocket {
private:
  std::list<AsyncWebSocketClient *> clients;
  size_t maxClients = 10;     // Limit maximum connections
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
        espWifi->log("❌ Failed to create WebSocket");
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
        // Check if we're at the connection limit using total count
        if (clients.size() >= maxClients) {
          espWifi->logf(
              "⚠️  Connection limit reached (%d), rejecting new connection\n",
              maxClients);
          client->close();
          return;
        }

        clients.push_back(client);
        espWifi->logf("🔌 WebSocket Client Connected: 🔗\n");
        espWifi->logf("\tID: %d\n", client->id());
        espWifi->logf("\tPath: %s\n", socket->url());
        espWifi->logf("\tPort: %d\n", client->remotePort());
        espWifi->logf("\tIP: %s\n", client->remoteIP().toString().c_str());
        espWifi->logf("\tActive Clients: %d\n", clients.size());
      } else if (type == WS_EVT_DISCONNECT) {
        // Safely remove client from list
        auto it = std::find(clients.begin(), clients.end(), client);
        if (it != clients.end()) {
          espWifi->log("🔌 WebSocket Client Disconnected: ⛓️‍💥");
          espWifi->logf("\tID: %d\n", client->id());
          espWifi->logf("\tPath: %s\n", socket->url());
          espWifi->logf("\tPort: %d\n", client->remotePort());
          espWifi->logf("\tIP: %s\n", client->remoteIP().toString().c_str());
          espWifi->logf("\tActive Clients: %d\n", clients.size() - 1);
          espWifi->logf("\tTotal Clients: %d\n", clients.size());
          espWifi->logf("\tDisconnect Time: %lu ms\n", millis());

          clients.erase(it);
        }
      }

      if (onWsEvent)
        onWsEvent(server, client, type, arg, data, len, espWifi);
    });

    if (espWifi) {
      espWifi->initWebServer();
      espWifi->webServer->addHandler(socket);
      espWifi->log("🔌 WebSocket Started:");
      espWifi->logf("\tPath: %s\n", path.c_str());
    }
  }

  ~WebSocket() {
    // Close all connections first
    closeAll();

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

  // Clean up disconnected clients and return actual active count
  size_t activeClientCount() {
    size_t count = 0;
    auto it = clients.begin();
    while (it != clients.end()) {
      AsyncWebSocketClient *client = *it;
      if (client) {
        // Safely check client status
        try {
          if (client->status() == WS_CONNECTED) {
            count++;
            ++it;
          } else {
            // Remove disconnected client
            espWifi->log("🔌 WebSocket Client Removed: ⛓️‍💥");
            espWifi->logf("\tID: %d\n", client->id());
            espWifi->logf("\tPath: %s\n", socket->url());
            it = clients.erase(it);
          }
        } catch (...) {
          // If we can't access client status, remove it
          espWifi->log(
              "🔌 WebSocket Client Removed (invalid): ⛓️‍💥");
          it = clients.erase(it);
        }
      } else {
        // Remove null client
        it = clients.erase(it);
      }
    }
    return count;
  }

  // Get total clients in list (including potentially disconnected ones)
  size_t totalClientCount() const { return clients.size(); }

  // Set maximum number of clients allowed
  void setMaxClients(size_t max) { maxClients = max; }

  // Close all connections
  void closeAll() {
    for (auto client : clients) {
      if (client) {
        try {
          if (client->status() == WS_CONNECTED) {
            client->close();
          }
        } catch (...) {
          // Ignore errors when closing clients
        }
      }
    }
    clients.clear();
    if (espWifi)
      espWifi->log("🔌 All WebSocket connections closed");
  }
};
