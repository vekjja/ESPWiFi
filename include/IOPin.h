#ifndef IOPIN_H
#define IOPIN_H

#include "Arduino.h"

class IOPin {
 public:
  int _mode;
  int _pin;

  IOPin(int pin) {
    _pin = pin;
    _mode = OUTPUT;
    pinMode(_pin, _mode);
  };

  IOPin(int pin, int mode) {
    _pin = pin;
    _mode = mode;
    pinMode(_pin, _mode);
  };

  int pin() { return _pin; };

  // Pin Controls
  void setPinMode(int mode) {
    _mode = mode;
    pinMode(_pin, _mode);
  };

  boolean isHigh() { return digitalRead(_pin) == HIGH; };
  boolean isLow() { return digitalRead(_pin) == LOW; };

  boolean isOn() { return digitalRead(_pin) == HIGH; };
  boolean isOff() { return digitalRead(_pin) == LOW; };

  void off() { digitalWrite(_pin, LOW); };
  void on() { digitalWrite(_pin, HIGH); };

  void setLow() { digitalWrite(_pin, LOW); };
  void setHigh() { digitalWrite(_pin, HIGH); };

  // Read
  int readA() { return analogRead(_pin); };
  int readD() { return digitalRead(_pin); };

  // Write
  void writeA(int val) { analogWrite(_pin, val); };
  void writeD(int val) { digitalWrite(_pin, val); };

  // Tone Controls
  void startTone(int val) { tone(_pin, val); };
  void startTone(int val, int dur) { tone(_pin, val, dur); };
  void stopTone() { noTone(_pin); };
};
#endif
