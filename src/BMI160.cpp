#ifndef ESPWiFi_BMI160_H
#define ESPWiFi_BMI160_H

#include <Wire.h>

#include "BMI160/DFRobot_BMI160.h"
#include "ESPWiFi.h"

DFRobot_BMI160 bmi160;
const int bmi160_i2c_addr = 0x69;

bool ESPWiFi::startBMI160(uint8_t address) {
  scanI2CDevices();
  if (checkI2CDevice(address)) {
    // Initialize BMI160 using DFRobot library
    int8_t rslt = bmi160.I2cInit(address);

    if (rslt == BMI160_OK) {
      
      log("📲 BMI160 initialized successfully");
      return true;
    } else {
      logError("BMI160 initialization failed!");
      return false;
    }
  } else {
    logError("BMI160 sensor not detected at the specified I2C address: 0x" +
             String(address, HEX));
    return false;
  }
}

int8_t ESPWiFi::readGyro(int16_t *gyroData) {
  return bmi160.getGyroData(gyroData);
}

int8_t ESPWiFi::readAccelerometer(int16_t *accelData) {
  return bmi160.getAccelData(accelData);
}

void ESPWiFi::readGyro(float &x, float &y, float &z) {
  int16_t gyroData[3];
  readGyro(gyroData);
  // Convert raw values to degrees per second (dps)
  x = gyroData[0];
  y = gyroData[1];
  z = gyroData[2];
}

void ESPWiFi::readAccelerometer(float &x, float &y, float &z) {
  int16_t accelData[3];
  readAccelerometer(accelData);
  // Convert raw values to g (gravity)
  x = accelData[0];
  y = accelData[1];
  z = accelData[2];
}

// float getTemperature(String unit) {
//   if (!BMI160Initialized)
//     return 0;
//   int16_t rawTemp = BMI160.getTemperature(); // returns a 16-bit integer
//   // The temperature data is a signed 16-bit value where 0x0000 corresponds
//   // 23°C, and each least significant bit (LSB) represents approximately
//   // 0.00195°C.
//   float tempC = 23.0 + ((float)rawTemp) * 0.00195;
//   if (unit == "F") {
//     float tempF = tempC * 9.0 / 5.0 + 32.0; // Convert to Fahrenheit
//     return tempF;
//   }
//   return tempC;
// }

#endif  // ESPWiFi_BMI160_H