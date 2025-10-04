#ifndef ESPWiFi_RSSI_SOCKET
#define ESPWiFi_RSSI_SOCKET

#include <IntervalTimer.h>
#include <WebSocket.h>

#include "ESPWiFi.h"

IntervalTimer *rssiTimer = nullptr;
char rssiBuffer[16];

// Method to send RSSI data (can be called from anywhere)
void ESPWiFi::streamRSSI() {

  if (rssiWebSocket == nullptr) {
    return;
  }

  if (!rssiTimer) {
    rssiTimer = new IntervalTimer(1000, [this]() {
      int rssi = WiFi.RSSI() | 0;
      snprintf(rssiBuffer, sizeof(rssiBuffer), "%d", rssi);
      rssiWebSocket->textAll(String(rssiBuffer));
    });
  }
  rssiTimer->run();
}

void ESPWiFi::rssiConfigHandler() {
  if (config["rssi"]["enabled"]) {
    rssiWebSocket = new WebSocket("/rssi", this);
    log("🔗 RSSI WebSocket Started");
  } else {
    // Stop and clean up the timer first
    if (rssiTimer) {
      delete rssiTimer;
      rssiTimer = nullptr;
    }

    // Properly close and clean up the WebSocket
    if (rssiWebSocket) {
      // Close all client connections
      if (rssiWebSocket->socket) {
        rssiWebSocket->socket->closeAll();
      }
      // Delete the WebSocket instance
      delete rssiWebSocket;
      rssiWebSocket = nullptr;
    }

    log("🔗 RSSI WebSocket Stopped");
  }
}

#endif // ESPWiFi_RSSI_SOCKET