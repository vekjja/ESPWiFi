#include <list>

class WebSocket {
 private:
  std::list<AsyncWebSocketClient *> clients;
  size_t maxClients = 10; // Limit maximum connections

 public:
  AsyncWebSocket *socket = nullptr;

  WebSocket() {}

  WebSocket(String path, ESPWiFi *espWifi,
            void (*onWsEvent)(AsyncWebSocket *, AsyncWebSocketClient *,
                              AwsEventType, void *, uint8_t *,
                              size_t) = nullptr) {
    socket = new AsyncWebSocket(path.c_str());

    socket->onEvent([this, onWsEvent](AsyncWebSocket *server,
                                      AsyncWebSocketClient *client,
                                      AwsEventType type, void *arg,
                                      uint8_t *data, size_t len) {
      if (type == WS_EVT_CONNECT) {
        // Clean up any disconnected clients first
        activeClientCount(); // This will clean up stale connections
        
        // Check if we're at the connection limit using active count
        if (activeClientCount() >= maxClients) {
          Serial.printf("⚠️  Connection limit reached (%d), rejecting new connection\n", maxClients);
          client->close();
          return;
        }
        
        clients.push_back(client);
        Serial.printf("🔌 WebSocket Client Connected: 🔗\n");
        Serial.printf("\tID: %d\n", client->id());
        Serial.printf("\tPath: %s\n", socket->url());
        Serial.printf("\tPort: %d\n", client->remotePort());
        Serial.printf("\tIP: %s\n", client->remoteIP().toString().c_str());
        Serial.printf("\tActive Clients: %d\n", activeClientCount());
        Serial.printf("\tTotal in List: %d\n", clients.size());
      } else if (type == WS_EVT_DISCONNECT) {
        clients.remove(client);
        Serial.println("🔌 WebSocket Client Disconnected: ⛓️‍💥");
        Serial.printf("\tID: %d\n", client->id());
        Serial.printf("\tActive Clients: %d\n", activeClientCount());
        Serial.printf("\tTotal Clients: %d\n", clients.size());
      }

      if (onWsEvent) onWsEvent(server, client, type, arg, data, len);
    });

    espWifi->initWebServer();
    espWifi->webServer->addHandler(socket);
    Serial.println("🔌 WebSocket Started:");
    Serial.printf("\tPath: %s\n", path.c_str());
    Serial.printf("\tMax Clients: %d\n", maxClients);
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
        Serial.println("🔌 WebSocket Client Removed: ⛓️‍💥");
        Serial.printf("\tID: %d\n", (*it)->id());
        Serial.printf("\tPath: %s\n", socket->url());
        it = clients.erase(it);
      }
    }
    return count;
  }

  // Get total clients in list (including potentially disconnected ones)
  size_t totalClientCount() const {
    return clients.size();
  }

  // Set maximum number of clients allowed
  void setMaxClients(size_t max) {
    maxClients = max;
  }

  // Close all connections
  void closeAll() {
    for (auto client : clients) {
      if (client->status() == WS_CONNECTED) {
        client->close();
      }
    }
    clients.clear();
    Serial.println("🔌 All WebSocket connections closed");
  }

  // Debug method to print current client status
  void debugClients() {
    Serial.printf("🔍 WebSocket Debug - Path: %s\n", socket ? socket->url() : "null");
    Serial.printf("\tActive Clients: %d\n", activeClientCount());
    Serial.printf("\tTotal in List: %d\n", clients.size());
    Serial.printf("\tMax Allowed: %d\n", maxClients);
    
    int i = 0;
    for (auto client : clients) {
      Serial.printf("\tClient %d: ID=%d, Status=%s, IP=%s\n", 
                   i++, 
                   client->id(),
                   client->status() == WS_CONNECTED ? "CONNECTED" : "DISCONNECTED",
                   client->remoteIP().toString().c_str());
    }
  }
};
