#ifndef ESPWiFi_RSSI_SOCKET
#define ESPWiFi_RSSI_SOCKET

#include <IntervalTimer.h>
#include <WebSocket.h>

#include "ESPWiFi.h"

IntervalTimer *rssiTimer = nullptr;
char rssiBuffer[16];

// Method to send RSSI data (can be called from anywhere)
void ESPWiFi::streamRSSI() {

  if (!rssiWebSocket) {
    rssiWebSocket = new WebSocket("/rssi", this);
  }

  if (!rssiTimer) {
    rssiTimer = new IntervalTimer(1000, [this]() {
      int rssi = WiFi.RSSI() | 0;
      snprintf(rssiBuffer, sizeof(rssiBuffer), "%d", rssi);
      if (rssiWebSocket) {
        rssiWebSocket->textAll(String(rssiBuffer));
      }
    });
  }
  rssiTimer->run();
}

#endif // ESPWiFi_RSSI_SOCKET