#include <list>

class WebSocket {
 private:
  std::list<AsyncWebSocketClient *> clients;

 public:
  AsyncWebSocket *socket;

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
        clients.push_back(client);
      } else if (type == WS_EVT_DISCONNECT) {
        clients.remove(client);
      }

      if (onWsEvent) onWsEvent(server, client, type, arg, data, len);
    });

    espWifi->initWebServer();
    espWifi->webServer->addHandler(socket);
    Serial.printf("⛓️  WebSocket Started: %s\n", path.c_str());
  }

  ~WebSocket() {
    if (socket) {
      delete socket;
      socket = nullptr;
    }
  }

  void textAll(const String &message) { socket->textAll(message); }

  void binaryAll(const char *data, size_t len) {
    socket->binaryAll((const uint8_t *)data, len);
  }

  size_t activeClientCount() const {
    size_t count = 0;
    for (auto client : clients) {
      if (client->status() == WS_CONNECTED) count++;
    }
    return count;
  }
};
