// WebServer.cpp - Stubbed for now
#include "ESPWiFi.h"

void ESPWiFi::initWebServer() {
  log(INFO, "🗄️ Web server not implemented yet");
}

void ESPWiFi::startWebServer() {
  webServerStarted = false;
  log(INFO, "🗄️ HTTP Web Server not implemented yet");
}

void ESPWiFi::addCORS(void *req) {
  // Stub
}

void ESPWiFi::handleCorsPreflight(void *req) {
  // Stub
}

void ESPWiFi::sendJsonResponse(void *req, int statusCode,
                               const std::string &jsonBody) {
  // Stub
}

void ESPWiFi::srvAll() {
  // Stub
}

void ESPWiFi::srvRoot() {
  // Stub
}

void ESPWiFi::srvInfo() {
  // Stub
}

void ESPWiFi::srvFiles() {
  // Stub
}

void ESPWiFi::srvConfig() {
  // Stub
}

void ESPWiFi::srvRestart() {
  // Stub
}
