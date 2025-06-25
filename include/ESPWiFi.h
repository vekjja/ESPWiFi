#ifndef ESPWiFi_H
#define ESPWiFi_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFiClient.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#else
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

class ESPWiFi {
 public:
  JsonDocument config;
  AsyncWebServer* webServer = nullptr;
  String configFile = "/config.json";

  int connectTimeout = 12000;  // 12 seconds
  void (*connectSubroutine)() = nullptr;

  void disableLowPowerSleep() {
    WiFi.setSleep(false);
    Serial.println("🚫 🔋 Low Power Sleep Disabled");
  }

  // Config
  void saveConfig();
  void readConfig();
  void defaultConfig();

  // WiFi
  void startAP();
  void startWiFi();
  void startClient();
  int selectBestChannel();

  // mDNS
  void startMDNS();
  void mDSNUpdate();

  // Web Server
  void initWebServer();
  void startWebServer();
  void addCORS(AsyncWebServerResponse* response);

  // GPIO
  void startGPIO();

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String& filename);
  void runAtInterval(unsigned int interval, unsigned long& lastIntervalRun,
                     std::function<void()> functionToRun);

  // SSID Spoofing
#ifdef ESP8266
  void initSSIDSpoof();
  void handleSSIDSpoof();
  bool ssidSpoofEnabled = false;
  unsigned long lastSSIDPacketTime = 0;
#endif

  // #############################################################################################################
 private:
  void handleCorsPreflight(AsyncWebServerRequest* request);
};

#endif  // ESPWiFi