#ifndef ESPWiFi_WIFI
#define ESPWiFi_WIFI

#include <WebSocket.h>
#include <WiFiClient.h>

#include "ESPWiFi.h"
#include <ESPmDNS.h>
#include <WiFi.h>

void ESPWiFi::startWiFi() {

  if (!config["wifi"]["enabled"].as<bool>()) {
    log(INFO, "🛜  WiFi Disabled");
    return;
  }

  String mode = config["wifi"]["mode"];
  mode.toLowerCase();
  if (strcmp(mode.c_str(), "client") == 0) {
    startClient();
  } else if (strcmp(mode.c_str(), "accessPoint") == 0 ||
             strcmp(mode.c_str(), "ap") == 0) {
    startAP();
  } else {
    log(WARNING, "⚠️  Invalid Mode: %s", mode.c_str());
    config["wifi"]["mode"] = "accessPoint"; // Ensure mode is set to accesspoint
    startAP();
  }
}

void ESPWiFi::startClient() {

  String ssid = config["wifi"]["client"]["ssid"];
  String password = config["wifi"]["client"]["password"];

  if (ssid.isEmpty()) {
    log(WARNING, "Warning: SSID: Cannot be empty, starting Access Point");
    config["wifi"]["mode"] = "accessPoint";
    startAP();
    return;
  }
  log(INFO, "🔗 Connecting to WiFi Network:");
  log(DEBUG, "\tSSID: %s", ssid.c_str());
  log(DEBUG, "\tPassword: **********");
  log(DEBUG, "\tMAC: %s", WiFi.macAddress().c_str());
  Serial.print("\t");

  WiFi.disconnect(true);      // Ensure clean start
  delay(100);                 // Allow time for disconnect
  WiFi.mode(WIFI_STA);        // Station mode only
  WiFi.begin(ssid, password); // Start connection

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < (unsigned long)connectTimeout) {
    if (connectSubroutine != nullptr) {
      connectSubroutine();
    }
    Serial.print(".");
    delay(30); // Wait for connection
  }
  Serial.println("");

  if (WiFi.status() != WL_CONNECTED) {
    log(ERROR, "🛜 Failed to connect to WiFi");
    config["wifi"]["mode"] = "accessPoint";
    startAP();
    return;
  }
  log(INFO, "🛜  WiFi Connected");
  log(DEBUG, "\tHostname: %s", WiFi.getHostname());
  log(DEBUG, "\tIP Address: %s", WiFi.localIP().toString().c_str());
  log(DEBUG, "\tSubnet: %s", WiFi.subnetMask().toString().c_str());
  log(DEBUG, "\tGateway: %s", WiFi.gatewayIP().toString().c_str());
  log(DEBUG, "\tDNS: %s", WiFi.dnsIP().toString().c_str());
  log(DEBUG, "\tRSSI: %d dBm", WiFi.RSSI());
  log(DEBUG, "\tChannel: %d", WiFi.channel());
}

int ESPWiFi::selectBestChannel() {
  // client count for each channel, 14 for 2.4 GHz band
  int channels[14] = {0};
  int numNetworks = WiFi.scanNetworks();
  for (int i = 0; i < numNetworks; i++) {
    int channel = WiFi.channel(i);
    if (channel > 0 &&
        channel <= 13) { // Ensure the channel is within a valid range
      channels[channel]++;
    }
  }
  int leastCongestedChannel = 1; // Default to channel 1
  for (int i = 1; i <= 13; i++) {
    if (channels[i] < channels[leastCongestedChannel]) {
      leastCongestedChannel = i;
    }
  }
  return leastCongestedChannel;
}

void ESPWiFi::startAP() {
  String hostname = String(WiFi.getHostname());

  String ssid = config["wifi"]["ap"]["ssid"].as<String>() + "-" + hostname;
  String password = config["wifi"]["ap"]["password"];
  log(INFO, "📡 Starting Access Point");
  log(DEBUG, "\tSSID: %s", ssid.c_str());
  log(DEBUG, "\tPassword: %s", password.c_str());
  int bestChannel = selectBestChannel();
  log(DEBUG, "\tChannel: %d", bestChannel);

  WiFi.softAP(ssid, password, bestChannel);
  if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    log(ERROR, "Failed to start Access Point");
    return;
  }
  log(DEBUG, "\tIP Address: %s", WiFi.softAPIP().toString().c_str());
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW); // Turn on LED to indicate AP mode
#endif
}

void ESPWiFi::startMDNS() {
  if (!config["wifi"]["enabled"].as<bool>()) {
    log(INFO, "🏷️  mDNS Disabled");
    return;
  }

  String domain = config["deviceName"];
  if (!MDNS.begin(domain)) {
    log(ERROR, "Error setting up MDNS responder!");
  } else {
    MDNS.addService("http", "tcp", 80);
    log(INFO, "🏷️  mDNS Started");
    domain.toLowerCase();
    log(DEBUG, "\tDomain Name: %s.local", domain.c_str());
  }
}

#endif // ESPWiFi_WIFI