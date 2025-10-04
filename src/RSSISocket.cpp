#ifndef ESPWiFi_RSSI_SOCKET
#define ESPWiFi_RSSI_SOCKET

#include <IntervalTimer.h>
#include <WebSocket.h>

#include "ESPWiFi.h"

IntervalTimer *rssiTimer = nullptr;
char rssiBuffer[16];

// Method to send RSSI data (can be called from anywhere)
void ESPWiFi::streamRSSI() {
  if (rssiWebSocket == nullptr || !config["rssi"]["enabled"]) {
    return;
  }

  // Only create timer if it doesn't exist
  if (rssiTimer == nullptr) {
    rssiTimer = new IntervalTimer(1000, [this]() {
      // Check if WebSocket still exists before sending
      if (rssiWebSocket != nullptr) {
        int rssi = WiFi.RSSI() | 0;
        snprintf(rssiBuffer, sizeof(rssiBuffer), "%d", rssi);
        rssiWebSocket->textAll(String(rssiBuffer));
      }
    });
  }

  // Only run timer if it exists
  if (rssiTimer != nullptr) {
    rssiTimer->run();
  }
}

void ESPWiFi::rssiConfigHandler() {

  bool rssiEnabled = config["rssi"]["enabled"];
  bool rssiWebSocketExists = rssiWebSocket != nullptr;

  if (rssiEnabled && !rssiWebSocketExists) {
    rssiWebSocket = new WebSocket("/rssi", this);
    log("🔗 RSSI WebSocket Started");

  } else if (!rssiEnabled && rssiWebSocketExists) {
    // Stop and clean up the timer first
    if (rssiTimer) {
      rssiTimer = nullptr; // Just set to nullptr, don't delete
    }

    // Store reference to socket before cleanup
    AsyncWebSocket *socketToRemove = nullptr;
    if (rssiWebSocket && rssiWebSocket->socket) {
      socketToRemove = rssiWebSocket->socket;

      // Close all client connections first
      socketToRemove->closeAll();

      // Remove the AsyncWebSocket handler from the server
      webServer->removeHandler(socketToRemove);

      // Clear the socket reference to prevent destructor from deleting it
      rssiWebSocket->socket = nullptr;
    }

    // Delete the WebSocket wrapper object
    if (rssiWebSocket) {
      delete rssiWebSocket;
      rssiWebSocket = nullptr;
    }

    log("⛓️‍💥 RSSI WebSocket Stopped");
  }
}

#endif // ESPWiFi_RSSI_SOCKET