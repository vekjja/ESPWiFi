#ifndef CLOUDCTL_H
#define CLOUDCTL_H

#include "Cloud.h"
#include <ArduinoJson.h>
#include <functional>

/**
 * @brief CloudCtl - Cloud connection for JSON control messages
 *
 * Extends Cloud base class to handle bidirectional JSON messaging
 * for device control commands and responses.
 */
class CloudCtl : public Cloud {
public:
  // Message handler callback for JSON messages
  using OnMessageCb = std::function<void(JsonDocument &message)>;

  CloudCtl();
  ~CloudCtl() override;

  // Send JSON message to cloud (forwarded to connected UI clients)
  bool sendMessage(const JsonDocument &message);
  bool sendText(const char *message);

  // Set message handler (receives JSON messages from UI clients)
  void onMessage(OnMessageCb callback) { onMessage_ = callback; }

protected:
  OnMessageCb onMessage_ = nullptr;

  // Override to handle JSON messages
  void handleMessage(const uint8_t *data, size_t len, bool isBinary) override;
};

#endif // CLOUDCTL_H
