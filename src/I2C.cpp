#ifndef I2C_H
#define I2C_H

#include <Arduino.h>
#include <Wire.h>

#include "ESPWiFi.h"

bool ESPWiFi::checkI2CDevice(uint8_t address) {
  Wire.beginTransmission(address);
  if (Wire.endTransmission() == 0) {
    return true; // Device found at this address
  }
  return false; // No device found
}

void ESPWiFi::scanI2CDevices() {
  byte error, address;
  int nDevices;

  Wire.begin();
  log(INFO, "🔎 Scanning for I2C Devices...");

  nDevices = 0;
  for (address = 1; address < 127; address++) {
    unsigned long startTime = millis();
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      log(INFO, "🎛️ I2C device found at address 0x%02X", address);

      nDevices++;
    } else if (error == 4) {
      log(ERROR, "Unknown error at address 0x");
      if (address < 16)
        log(INFO, "0");
      log(INFO, "%s HEX", String(address).c_str());
    }
  }
  if (nDevices == 0)
    log(INFO, "No I2C Devices Found");
}

#endif