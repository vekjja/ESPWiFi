#ifndef ESPWiFi_H
#define ESPWiFi_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <IntervalTimer.h>
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
  String configFile = "/config.json";
  AsyncWebServer* webServer = nullptr;

  IntervalTimer s10Timer{10000};  // 10 seconds
  IntervalTimer s1Timer{1000};    // 1 second

  int connectTimeout = 12000;  // 12 seconds
  void (*connectSubroutine)() = nullptr;

  void startSerial(int baudRate = 115200) {
    if (Serial) {
      return;
    }
    Serial.begin(baudRate);
    delay(900);
    Serial.println("🔌 Serial Started at " + String(baudRate) + " baud");
  }
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

  // Camera
  void startCamera();
  void streamCamera(int frameRate = 10);
  void takeSnapshot(String filePath = "/snapshot.jpg");

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String& filename);
  void runAtInterval(unsigned int interval, unsigned long& lastIntervalRun,
                     std::function<void()> functionToRun);

 private:
  void handleCorsPreflight(AsyncWebServerRequest* request);
};

#endif  // ESPWiFi