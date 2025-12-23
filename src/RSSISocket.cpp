#ifndef ESPWiFi_RSSI_SOCKET
#define ESPWiFi_RSSI_SOCKET

#include <IntervalTimer.h>
#include <WebSocket.h>

#include "ESPWiFi.h"

IntervalTimer *rssiTimer = nullptr;
char rssiBuffer[16];

// Initialize RSSI WebSocket - should be called after web server is started
void ESPWiFi::startRSSIWebSocket() {
  if (!config["wifi"]["enabled"].as<bool>()) {
    logln("🔗 RSSI WebSocket Disabled");
    return;
  }
  // Only create RSSI WebSocket if WiFi is enabled (RSSI requires WiFi)
  if (rssiWebSocket == nullptr && config["wifi"]["enabled"].as<bool>()) {
    rssiWebSocket = new WebSocket("/rssi", this);
  }
}

// Method to send RSSI data (can be called from anywhere)
void ESPWiFi::streamRSSI() {
  // Ensure WebSocket is initialized and WiFi is enabled
  if (rssiWebSocket == nullptr && config["wifi"]["enabled"].as<bool>()) {
    startRSSIWebSocket();
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
  rssiTimer->run();
}

#endif // ESPWiFi_RSSI_SOCKET