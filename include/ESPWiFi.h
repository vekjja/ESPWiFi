#ifndef ESPWiFi_h
#define ESPWiFi_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncWebSocket.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClient.h>

class ESPWiFi {
 public:
  JsonDocument config;
  AsyncWebSocket ws{"/ws"};
  AsyncWebServer webServer{80};
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
  void startWebServer();
  void addCORS(AsyncWebServerResponse* response);

  // WebSocket
  void startWebSocket();
  void sendWebSocketMessage(const String& message);

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

// Helper class for interval timing
class IntervalTimer {
  unsigned long lastRun = 0;
  unsigned int interval;

 public:
  IntervalTimer(unsigned int ms) : interval(ms) {}
  void setInterval(unsigned int ms) { interval = ms; }
  bool shouldRun() {
    unsigned long now = millis();
    if (now - lastRun >= interval) {
      lastRun = now;
      return true;
    }
    return false;
  }
  void reset() { lastRun = millis(); }
};
#endif  // ESPWiFi_h