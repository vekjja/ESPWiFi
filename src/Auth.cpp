// Auth.cpp - Stubbed for now
#include "ESPWiFi.h"

std::string ESPWiFi::generateToken() {
  // TODO: Implement token generation
  return "stub_token";
}

bool ESPWiFi::authEnabled() { return config["auth"]["enabled"].as<bool>(); }

// Stub for web server auth endpoint
void ESPWiFi::srvAuth() {
  // Will implement later with HTTP server
}
