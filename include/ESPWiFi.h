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

  int connectTimeout = 9000;  // 9 seconds

  void (*connectSubroutine)() = nullptr;

  ESPWiFi() {}

  void disableLowPowerSleep() {
#ifdef ESP8266
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
    WiFi.setSleep(false);
#endif
    Serial.println("🔋 Low Power Sleep Disabled");
  }

  void start() {
    Serial.begin(115200);
    delay(500);  // allow time for USB to connect

    if (!LittleFS.begin()) {
      Serial.println("An Error has occurred while mounting LittleFS");
    }

    readConfig();

    String mode = config["mode"];
    mode.toLowerCase();
    if (strcmp(mode.c_str(), "client") == 0 ||
        strcmp(mode.c_str(), "station") == 0 ||
        strcmp(mode.c_str(), "sta") == 0) {
      connectToWifi();
    } else if (strcmp(mode.c_str(), "ap") == 0 ||
               strcmp(mode.c_str(), "accesspoint") == 0 ||
               strcmp(mode.c_str(), "access point") == 0) {
      startAP();
    } else {
      Serial.println("⚠️  Invalid Mode: " + mode);
      config["mode"] = "ap";  // Ensure mode is set to AP
      startAP();
    }

    startWebServer();
    startMDNS();
    Serial.println("\n✅ ESPWiFi Started Successfully");
  }

  // Config
  void saveConfig();
  void readConfig();
  void defaultConfig();
  void resetConfig();

  // WiFi
  void connectToWifi();
  void handleClient();
  void startAP();
  int selectBestChannel();

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String& filename);
  // String makeHTTPSRequest(const String& method, const String& url,
  //                         const String& token = "",
  //                         const String& contentType = "",
  //                         const String& payload = "");
  void runAtInterval(unsigned int interval, unsigned long& lastIntervalRun,
                     std::function<void()> functionToRun);

  // GPIO
  void initGPIO();

  // DeAuth
  void enableDeAuth();

  // SSID Spoofing
  void initSSIDSpoof();
  void handleSSIDSpoof();
  bool ssidSpoofEnabled = false;
  unsigned long lastSSIDPacketTime = 0;

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
  void handleCorsPreflight();
  void listFilesHandler();
};
#endif  // ESPWiFi_h