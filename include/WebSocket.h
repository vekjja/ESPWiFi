#include "ESPWiFi.h"
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
    socket = new AsyncWebSocket(path.c_str());

    socket->onEvent([this, onWsEvent, espWifi](AsyncWebSocket *server,
                                               AsyncWebSocketClient *client,
                                               AwsEventType type, void *arg,
                                               uint8_t *data, size_t len) {
      if (!espWifi)
        return; // Early return if no ESPWiFi instance

      if (type == WS_EVT_CONNECT) {

        int clientCount =
            activeClientCount(); // This will clean up stale connections

        // Check if we're at the connection limit using active count
        if (clientCount >= maxClients) {
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
        espWifi->logf("\tActive Clients: %d\n", clientCount);
      } else if (type == WS_EVT_DISCONNECT) {
        clients.remove(client);
        espWifi->log("🔌 WebSocket Client Disconnected: ⛓️‍💥");
        espWifi->logf("\tID: %d\n", client->id());
        espWifi->logf("\tActive Clients: %d\n", clientCount);
        espWifi->logf("\tTotal Clients: %d\n", clients.size());
      }

      if (onWsEvent)
        onWsEvent(server, client, type, arg, data, len, espWifi);
    });

    espWifi->initWebServer();
    espWifi->webServer->addHandler(socket);
    espWifi->log("🔌 WebSocket Started:");
    espWifi->logf("\tPath: %s\n", path.c_str());
  }

  ~WebSocket() {
    if (socket) {
      delete socket;
      socket = nullptr;
    }
  }

  operator bool() const { return socket != nullptr; }

  void textAll(const String &message) { socket->textAll(message); }

  void binaryAll(const char *data, size_t len) {
    socket->binaryAll((const uint8_t *)data, len);
  }

  // Clean up disconnected clients and return actual active count
  size_t activeClientCount() {
    size_t count = 0;
    auto it = clients.begin();
    while (it != clients.end()) {
      if ((*it)->status() == WS_CONNECTED) {
        count++;
        ++it;
      } else {
        // Remove disconnected client
        espWifi->log("🔌 WebSocket Client Removed: ⛓️‍💥");
        espWifi->logf("\tID: %d\n", (*it)->id());
        espWifi->logf("\tPath: %s\n", socket->url());
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
      if (client->status() == WS_CONNECTED) {
        client->close();
      }
    }
    clients.clear();
    if (espWifi)
      espWifi->log("🔌 All WebSocket connections closed");
  }
};
