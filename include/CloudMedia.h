#ifndef CLOUDMEDIA_H
#define CLOUDMEDIA_H

#include "Cloud.h"

/**
 * @brief CloudMedia - Cloud connection for binary media streaming
 *
 * Extends Cloud base class to handle high-bandwidth binary streaming
 * for camera frames, audio, and other media data.
 * Optimized for throughput - no JSON parsing overhead.
 */
class CloudMedia : public Cloud {
public:
  CloudMedia();
  ~CloudMedia() override;

  // Send binary data to cloud (forwarded to connected UI clients)
  bool sendBinary(const uint8_t *data, size_t len);

protected:
  // Override to ignore incoming messages (media is one-way: device -> UI)
  void handleMessage(const uint8_t *data, size_t len, bool isBinary) override;
};

#endif // CLOUDMEDIA_H
