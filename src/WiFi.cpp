// WiFi.cpp - Stubbed for now
#include "ESPWiFi.h"
#include "esp_event.h"
#include "esp_wifi.h"

void ESPWiFi::startWiFi() { log(INFO, "🛜 WiFi not implemented yet"); }

void ESPWiFi::startClient() {
  // Stub
}

void ESPWiFi::startAP() {
  // Stub
}

int ESPWiFi::selectBestChannel() { return 1; }

// IPAddress ESPWiFi::localIP() { return IPAddress(0, 0, 0, 0); }

// IPAddress ESPWiFi::softAPIP() { return IPAddress(192, 168, 4, 1); }

std::string ESPWiFi::macAddress() {
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(macStr);
}

void ESPWiFi::startMDNS() { log(INFO, "🌐 mDNS not implemented yet"); }
