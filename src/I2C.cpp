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
  logln("🔎 Scanning for I2C Devices...");

  nDevices = 0;
  for (address = 1; address < 127; address++) {
    unsigned long startTime = millis();
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      logln("🎛️ I2C device found at address 0x" +
          (address < 16 ? "0" : String(address, HEX)));

      nDevices++;
    } else if (error == 4) {
      logError("Unknown error at address 0x");
      if (address < 16)
        logln("0");
      logln(String(address) + " HEX");
    }
  }
  if (nDevices == 0)
    logln("No I2C Devices Found");
}

#endif