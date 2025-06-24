#ifndef ESPWIFI_INTERVALTIMER
#define ESPWIFI_INTERVALTIMER
#include <Arduino.h>

// Helper class for interval timing
class IntervalTimer {
  unsigned long lastRun = 0;
  unsigned int interval;

 public:
  IntervalTimer(unsigned int ms) : interval(ms) {}
  void setInterval(unsigned int ms) { interval = ms; }
  bool shouldRun() {
    unsigned long now = millis();
    if (now - lastRun >= interval) {
      lastRun = now;
      return true;
    }
    return false;
  }
  void reset() { lastRun = millis(); }
};

#endif  // ESPWIFI_INTERVALTIMER