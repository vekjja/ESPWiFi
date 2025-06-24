#ifndef ESPWiFi_h
#define ESPWiFi_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiClient.h>

#ifdef ESP8266
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#define WebServer ESP8266WebServer
#else
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#endif

class ESPWiFi {
 public:
  WebServer webServer;
  JsonDocument config;
  String configFile = "/config.json";

  int connectTimeout = 12000;  // 12 seconds
  void (*connectSubroutine)() = nullptr;

  void disableLowPowerSleep() {
#ifdef ESP8266
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
    WiFi.setSleep(false);
#endif
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
  void handleClient();
  int selectBestChannel();

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
  void startMDNS();
  void startWebServer();
  void listFilesHandler();
  void handleCorsPreflight();
};
#endif  // ESPWiFi_h