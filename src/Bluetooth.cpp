// Bluetooth.cpp - Stubbed for now
#include "ESPWiFi.h"

bool ESPWiFi::startBluetooth() {
  bluetoothStarted = false;
  log(INFO, "📡 Bluetooth not implemented yet");
  return false;
}

void ESPWiFi::stopBluetooth() { bluetoothStarted = false; }

bool ESPWiFi::isBluetoothConnected() { return false; }

void ESPWiFi::scanBluetoothDevices() {
  // Stub
}

void ESPWiFi::bluetoothConfigHandler() {
  config["bluetooth"]["enabled"] = false;
}

void ESPWiFi::checkBluetoothConnectionStatus() {
  // Stub
}
