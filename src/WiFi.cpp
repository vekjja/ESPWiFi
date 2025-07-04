#ifndef ESPWiFi_WIFI
#define ESPWiFi_WIFI

#include <WebSocket.h>
#include <WiFiClient.h>

#include "ESPWiFi.h"

#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#endif

void ESPWiFi::startWiFi() {
  readConfig();

  String mode = config["mode"];
  mode.toLowerCase();
  if (strcmp(mode.c_str(), "client") == 0) {
    startClient();
  } else if (strcmp(mode.c_str(), "accesspoint") == 0 ||
             strcmp(mode.c_str(), "ap") == 0) {
    startAP();
  } else {
    log("⚠️  Invalid Mode: " + mode);
    config["mode"] = "accessPoint";  // Ensure mode is set to accesspoint
    startAP();
  }
}

void ESPWiFi::startClient() {
  readConfig();
  String ssid = config["client"]["ssid"];
  String password = config["client"]["password"];

  if (ssid.isEmpty()) {
    log("⚠️  Warning: SSID or Password: Cannot be empty");
    config["mode"] = "accessPoint";
    startAP();
    return;
  }
  log("🔗 Connecting to Network:");
  log("\tSSID: " + ssid);
  log("\tPassword: " + password);
  logf("\tMAC: %s\n", WiFi.macAddress().c_str());
  Serial.print("\t");

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
  Serial.print("\n");

  if (WiFi.status() != WL_CONNECTED) {
    logError("Failed to connect to WiFi");
    config["mode"] = "accessPoint";
    startAP();
    return;
  }
  log("🛜  WiFi Connected:");
  logf("\tIP Address: %s\n", WiFi.localIP().toString());
}

int ESPWiFi::selectBestChannel() {
  // client count for each channel, 14 for 2.4 GHz band
  int channels[14] = {0};
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
  readConfig();
  String ssid = config["ap"]["ssid"];
  String password = config["ap"]["password"];
  log("\n📡 Starting Access Point:");
  log("\tSSID: " + ssid);
  log("\tPassword: " + password);
  int bestChannel = selectBestChannel();
  logf("\tChannel: ");
  log(bestChannel);

  WiFi.softAP(ssid, password, bestChannel);
  if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    logError("Failed to start Access Point");
    return;
  }
  logf("\tIP Address: ");
  log(WiFi.softAPIP());
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);  // Turn on LED to indicate AP mode
}

void ESPWiFi::startMDNS() {
  readConfig();

  String domain = config["mdns"];
  if (!MDNS.begin(domain)) {
    logError("Error setting up MDNS responder!");
  } else {
    MDNS.addService("http", "tcp", 80);
    log("📛 mDNS Started:");
    domain.toLowerCase();
    log("\tDomain Name: " + domain + ".local");
  }
}

#ifdef ESP8266
void ESPWiFi::updateMDNS() {
  static IntervalTimer mDNSUpdateTimer(1000);
  if (mDNSUpdateTimer.shouldRun()) {
    MDNS.update();
  }
}
#endif

#endif  // ESPWiFi_WIFI