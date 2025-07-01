#ifndef ESPWIFI_INTERVALTIMER
#define ESPWIFI_INTERVALTIMER
#include <Arduino.h>

#include <functional>  // Include for std::function

// Helper class for interval timing
class IntervalTimer {
  unsigned long lastRun = 0;
  unsigned int interval;
  std::function<void()> runFunc = nullptr;  // Use std::function for callback

 public:
  void reset() { lastRun = millis(); }
  void setInterval(unsigned int ms) { interval = ms; }

  IntervalTimer(unsigned int ms, std::function<void()> callback = nullptr)
      : interval(ms), runFunc(callback) {}

  void setCallback(std::function<void()> callback) { runFunc = callback; }

  bool shouldRun(unsigned int ms = 0) {
    if (ms > 0) {
      interval = ms;
    }
    unsigned long now = millis();
    if (now - lastRun >= interval) {
      lastRun = now;
      if (runFunc) {
        runFunc();
      }
      return true;
    }
    return false;
  }

  void run() { shouldRun(); }
};

#endif  // ESPWIFI_INTERVALTIMER