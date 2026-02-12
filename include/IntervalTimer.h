#ifndef ESPWiFi_INTERVALTIMER
#define ESPWiFi_INTERVALTIMER

#include "esp_timer.h"
#include <cstdint>

// Low-overhead interval helper:
// - No std::function (avoids heap + type erasure)
// - Uses esp_timer_get_time() (microseconds) for long-uptime safety
class IntervalTimer {
public:
  using Callback = void (*)(void *ctx);

private:
  int64_t lastRunUs_ = 0;
  uint32_t intervalUs_ = 0;
  Callback cb_ = nullptr;
  void *cbCtx_ = nullptr;

public:
  IntervalTimer() = default;
  explicit IntervalTimer(uint32_t intervalMs, Callback cb = nullptr,
                         void *cbCtx = nullptr)
      : lastRunUs_(0), intervalUs_(intervalMs * 1000U), cb_(cb), cbCtx_(cbCtx) {
  }

  void reset() { lastRunUs_ = esp_timer_get_time(); }
  void resetAt(int64_t nowUs) { lastRunUs_ = nowUs; }

  void setIntervalMs(uint32_t intervalMs) { intervalUs_ = intervalMs * 1000U; }
  uint32_t intervalMs() const { return intervalUs_ / 1000U; }

  void setCallback(Callback cb, void *cbCtx = nullptr) {
    cb_ = cb;
    cbCtx_ = cbCtx;
  }

  bool shouldRun() { return shouldRunAt(esp_timer_get_time()); }

  bool shouldRunAt(int64_t nowUs) {
    // interval == 0 => always run
    if (intervalUs_ == 0) {
      lastRunUs_ = nowUs;
      if (cb_) {
        cb_(cbCtx_);
      }
      return true;
    }

    if (lastRunUs_ == 0) {
      lastRunUs_ = nowUs;
      if (cb_) {
        cb_(cbCtx_);
      }
      return true;
    }

    if ((nowUs - lastRunUs_) >= (int64_t)intervalUs_) {
      lastRunUs_ = nowUs;
      if (cb_) {
        cb_(cbCtx_);
      }
      return true;
    }

    return false;
  }
};

#endif // ESPWiFi_INTERVALTIMER