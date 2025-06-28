#ifndef ESPWIFI_WIFI
#define ESPWIFI_WIFI

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
    config["mode"] = "accessPoint"; // Ensure mode is set to accesspoint
    startAP();
  }
}

void ESPWiFi::startClient() {
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
  logf("\t");

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
    logf(".");
    delay(30); // Wait for connection
  }

  if (WiFi.status() != WL_CONNECTED) {
    log("\n❌ Failed to connect to WiFi");
    config["mode"] = "accessPoint";
    startAP();
    return;
  }
  log("");
  log("\n🛜  WiFi Connected:");
  logf("\tIP Address: ");
  log(WiFi.localIP().toString());
}

int ESPWiFi::selectBestChannel() {
  int channels[14] = {
      0}; // Array to hold channel usage counts, 14 for 2.4 GHz band
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
    log("❌ Failed to start Access Point");
    return;
  }
  logf("\tIP Address: ");
  log(WiFi.softAPIP());
}

void ESPWiFi::mDSNUpdate() {
#ifdef ESP8266
  if (s1Timer.shouldRun()) {
    MDNS.update();
  }
#endif
}

void ESPWiFi::startMDNS() {
  String domain = config["mdns"];
  if (!MDNS.begin(domain)) {
    log("❌ Error setting up MDNS responder!");
  } else {
    MDNS.addService("http", "tcp", 80);
    log("📛 mDNS Started:");
    domain.toLowerCase();
    log("\tDomain Name: " + domain + ".local");
  }
}

#endif // ESPWIFI_WIFI