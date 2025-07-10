#ifdef ESPWiFi_BMI160_ENABLED
#ifndef ESPWiFi_BMI160_H
#define ESPWiFi_BMI160_H

#include <Wire.h>

#include "DFRobot_BMI160.h"
#include "ESPWiFi.h"

DFRobot_BMI160 bmi160;

const int bmi160_i2c_addr = 0x69;
const float bmi160_scale_factor = 16384.0; // Scale Factor for ±2g
const float bmi160_raw_data_conversion =
    32768.0; // Raw data conversion factor for BMI160

float convertRawGyro(int gRaw) {
  // convert the raw gyro data to degrees/second
  // we are using 250 degrees/seconds range
  // -250 maps to a raw value of -32768
  // +250 maps to a raw value of 32767
  return (gRaw * 250.0) / bmi160_raw_data_conversion;
  ;
}

float convertRawAccel(int raw, int offset) {
  // Assuming the full scale range is ±2g
  return (raw - offset) / bmi160_scale_factor;
}

bool ESPWiFi::startBMI160(uint8_t address) {
  scanI2CDevices();
  if (checkI2CDevice(address)) {
    // Initialize BMI160 using DFRobot library
    int8_t rslt = bmi160.I2cInit(address);

    if (rslt == BMI160_OK) {
      log("📲 BMI160 Started");
      return true;
    } else {
      logError("BMI160 Failed to Start!");
      return false;
    }
  } else {
    logError("BMI160 sensor not detected at the specified I2C address: 0x" +
             String(address, HEX));
    return false;
  }
}

int8_t ESPWiFi::readGyroscope(int16_t *gyroData) {
  return bmi160.getGyroData(gyroData);
}

int8_t ESPWiFi::readAccelerometer(int16_t *accelData) {
  return bmi160.getAccelData(accelData);
}

void ESPWiFi::readGyroscope(float &x, float &y, float &z) {
  int16_t gyroData[3];
  readGyroscope(gyroData);
  // Convert raw values to degrees per second (dps)
  x = convertRawGyro(gyroData[0]);
  y = convertRawGyro(gyroData[1]);
  z = convertRawGyro(gyroData[2]);
}

void ESPWiFi::readAccelerometer(float &x, float &y, float &z) {
  int16_t accelData[3];
  readAccelerometer(accelData);
  // Convert raw values to g (gravity) with default offsets
  const int defaultOffset = 0;
  x = convertRawAccel(accelData[0], defaultOffset);
  y = convertRawAccel(accelData[1], defaultOffset);
  z = convertRawAccel(accelData[2], defaultOffset);
}

float ESPWiFi::getTemperature(String unit) {
  // Read temperature data directly from BMI160 using Wire library
  // Temperature data is available at registers 0x20 and 0x21
  uint8_t tempData[2];
  int16_t rawTemp;

  // Read temperature registers (0x20 and 0x21) using Wire library
  Wire.beginTransmission(bmi160_i2c_addr);
  Wire.write(0x20); // Temperature register address
  if (Wire.endTransmission() == 0) {
    if (Wire.requestFrom(bmi160_i2c_addr, 2) == 2) {
      tempData[0] = Wire.read(); // LSB
      tempData[1] = Wire.read(); // MSB

      // Combine the two bytes into a 16-bit signed integer
      rawTemp = (int16_t)((tempData[1] << 8) | tempData[0]);
    } else {
      // If reading fails, return a default value
      return 23.0; // Default temperature
    }
  } else {
    // If communication fails, return a default value
    return 23.0; // Default temperature
  }

  // The temperature data is a signed 16-bit value where 0x0000 corresponds
  // 23°C, and each least significant bit (LSB) represents approximately
  // 0.00195°C.
  float tempC = 23.0 + ((float)rawTemp) * 0.00195;

  if (unit == "F") {
    float tempF = tempC * 9.0 / 5.0 + 32.0; // Convert to Fahrenheit
    return tempF;
  }
  return tempC;
}

#endif // ESPWiFi_BMI160_H
#endif // ESPWiFi_BMI160_ENABLED