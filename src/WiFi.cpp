#ifndef ESPWIFI_WIFI_H
#define ESPWIFI_WIFI_H

#include "ESPWiFi.h"

void ESPWiFi::startClient() {
  String ssid = config["client"]["ssid"];
  String password = config["client"]["password"];

  if (ssid.isEmpty()) {
    Serial.println("⚠️  Warning: SSID or Password: Cannot be empty");
    config["mode"] = "ap";
    startAP();
    return;
  }
  Serial.println("\n🛜  Connecting to Network:");
  Serial.println("\tSSID: " + ssid);
  Serial.println("\tPassword: " + password);

  WiFi.disconnect(true);       // Ensure clean start
  delay(100);                  // Allow time for disconnect
  WiFi.mode(WIFI_STA);         // Station mode only
  WiFi.begin(ssid, password);  // Start connection

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < (unsigned long)connectTimeout) {
    if (connectSubroutine != nullptr) {
      connectSubroutine();
    }
    Serial.print(".");
    delay(30);  // Wait for connection
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ Failed to connect to WiFi");
    config["mode"] = "ap";
    startAP();
    return;
  }
  Serial.println("");
  Serial.println("\n🔗 WiFi Connected:");
  Serial.print("\tIP Address: ");
  Serial.println(WiFi.localIP());
}

int ESPWiFi::selectBestChannel() {
  int channels[14] = {
      0};  // Array to hold channel usage counts, 14 for 2.4 GHz band
  int numNetworks = WiFi.scanNetworks();
  for (int i = 0; i < numNetworks; i++) {
    int channel = WiFi.channel(i);
    if (channel > 0 &&
        channel <= 13) {  // Ensure the channel is within a valid range
      channels[channel]++;
    }
  }
  int leastCongestedChannel = 1;  // Default to channel 1
  for (int i = 1; i <= 13; i++) {
    if (channels[i] < channels[leastCongestedChannel]) {
      leastCongestedChannel = i;
    }
  }
  return leastCongestedChannel;
}

void ESPWiFi::startAP() {
  String ssid = config["ap"]["ssid"];
  String password = config["ap"]["password"];
  Serial.println("\n📡 Starting Access Point:");
  Serial.println("\tSSID: " + ssid);
  Serial.println("\tPassword: " + password);
  int bestChannel = selectBestChannel();
  Serial.print("\tChannel: ");
  Serial.println(bestChannel);

  WiFi.softAP(ssid, password, bestChannel);
  if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println("❌ Failed to start Access Point");
    return;
  }
  Serial.print("\tIP Address: ");
  Serial.println(WiFi.softAPIP());
}

void ESPWiFi::handleClient() {
  webServer.handleClient();
#ifdef ESP8266
  MDNS.update();
  // if (ssidSpoofEnabled) {
  //   handleSSIDSpoof();
  // }
#endif
}

void ESPWiFi::startMDNS() {
  String domain = config["mdns"];
  if (!MDNS.begin(domain)) {
    Serial.println("❌ Error setting up MDNS responder!");
  } else {
    MDNS.addService("http", "tcp", 80);
    Serial.println("\tDomain Name: " + domain + ".local");
  }
}

#endif  // ESPWIFI_WIFI_H