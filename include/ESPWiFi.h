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
  JsonDocument config;

  String configFile = "/config.json";

  WebServer webServer;

  int connectTimeout = 12000;  // 9 seconds

  void (*connectSubroutine)() = nullptr;

  ESPWiFi() {}

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

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String& filename);
  void runAtInterval(unsigned int interval, unsigned long& lastIntervalRun,
                     std::function<void()> functionToRun);
  // String makeHTTPSRequest(const String& method, const String& url,
  //                         const String& token = "",
  //                         const String& contentType = "",
  //                         const String& payload = "");

  // GPIO
  void startGPIO();

  // SSID Spoofing
#ifdef ESP8266
  void initSSIDSpoof();
  void handleSSIDSpoof();
  bool ssidSpoofEnabled = false;
  unsigned long lastSSIDPacketTime = 0;
#endif

  // OpenAI
  // String openAI_URL = "https://api.openai.com";
  // String openAI_TTSEndpoint = "/v1/audio/speech";
  // String openAI_ChatEndpoint = "/v1/chat/completions";
  // String openAI_Chat(String text);
  // void openAI_TTS(String text, String filePath);

  // #############################################################################################################
 private:
  void startMDNS();
  void startWebServer();
  void listFilesHandler();
  void handleCorsPreflight();
};
#endif  // ESPWiFi_h