#ifndef ESPWIFI_RSSISOCKET
#define ESPWIFI_RSSISOCKET

#include "ESPWiFi.h"
#include <IntervalTimer.h>
#include <WebSocket.h>

// Using separate WebSocket for RSSI data provides:
// - Clean separation of concerns
// - Independent lifecycle management
// - Optimized for text data streaming
// - Easy to debug and maintain
static IntervalTimer rssiSocTimer(1000);
static WebSocket *rssiWebSocket = nullptr;

// Method to send RSSI data (can be called from anywhere)
void ESPWiFi::streamRssi() {
  if (!rssiWebSocket) {
    rssiWebSocket = new WebSocket("/rssi", this);
  } else {
    if (rssiSocTimer.shouldRun()) {
      // Static buffer to avoid repeated allocations
      static char rssiBuffer[16];
      int rssi = WiFi.RSSI() | 0;
      snprintf(rssiBuffer, sizeof(rssiBuffer), "%d", rssi);
      rssiWebSocket->textAll(String(rssiBuffer));
    }
  }
}

#endif // ESPWIFI_RSSISOCKET