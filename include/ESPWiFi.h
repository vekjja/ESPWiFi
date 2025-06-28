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
#elif defined(ESP32)
#include <ESPmDNS.h>
#include <WiFi.h>
#endif

class ESPWiFi {
public:
  JsonDocument config;
  String logFile = "/log.txt";
  String configFile = "/config.json";
  AsyncWebServer *webServer = nullptr;

  IntervalTimer s10Timer{10000}; // 10 seconds
  IntervalTimer s1Timer{1000};   // 1 second

  int connectTimeout = 12000; // 12 seconds
  void (*connectSubroutine)() = nullptr;

  void startSerial(int baudRate = 115200) {
    if (Serial) {
      return;
    }
    Serial.begin(baudRate);
    delay(900);
    log("⛓️  Serial Started:");
    logf("\tBaud Rate: %d\n", baudRate);
  }

  void startLittleFS() {
    // Check if LittleFS is already mounted using a static flag
    static bool fsMounted = false;
    if (fsMounted) {
      return;
    }

    if (!LittleFS.begin()) {
      log("❌  Failed to mount LittleFS");
      return;
    }

    fsMounted = true;
    log("💾 LittleFS mounted:");
    logf("\tSize: %d bytes\n", LittleFS.totalBytes());
    logf("\tUsed: %d bytes\n", LittleFS.usedBytes());
    logf("\tFree: %d bytes\n", LittleFS.totalBytes() - LittleFS.usedBytes());
  }

  void stopLowPowerSleep() {
    WiFi.setSleep(false);
    log("🚫 🔋 Low Power Sleep Disabled");
  }

  // Log
  void closeLog();
  void log(String message);
  void writeLog(String message);
  void logf(const char *format, ...);
  template <typename T> void log(T value) {
    String valueString = String(value);
    log(valueString);
    writeLog(valueString + "\n");
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
  void addCORS(AsyncWebServerResponse *response);

  // GPIO
  void startGPIO();

  // Camera
#ifdef ESPWiFi_CAMERA_ENABLED
  void startCamera();
  void streamCamera(int frameRate = 10);
  void takeSnapshot(String filePath = "/snapshot.jpg");
#endif

  // Utils
  String getContentType(String filename);
  String getFileExtension(const String &filename);
  void runAtInterval(unsigned int interval, unsigned long &lastIntervalRun,
                     std::function<void()> functionToRun);

private:
  void handleCorsPreflight(AsyncWebServerRequest *request);
};

#endif // ESPWiFi