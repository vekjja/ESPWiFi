// RSSISocket.cpp - Stubbed for now
#include "ESPWiFi.h"
#include "IntervalTimer.h"
#include "WebSocket.h"
#include "esp_wifi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT
// Single WS instance shared by startRSSIWebSocket() and streamRSSI()
static WebSocket s_rssiWs;
static bool s_rssiStarted = false;
#endif

void ESPWiFi::startRSSIWebSocket() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  log(WARNING, "📶 RSSI WebSocket disabled (CONFIG_HTTPD_WS_SUPPORT is off)");
  return;
#else
  if (s_rssiStarted) {
    return;
  }

  // Keep handler light: we don't expect inbound frames; we only stream RSSI
  // out. Small max message len keeps RX buffer tiny.
  s_rssiStarted = s_rssiWs.begin("/ws/rssi", this,
                                 /*onMessage*/ nullptr,
                                 /*onConnect*/ nullptr,
                                 /*onDisconnect*/ nullptr,
                                 /*maxMessageLen*/ 32);

  if (!s_rssiStarted) {
    log(ERROR, "📶 RSSI WebSocket failed to start");
  }
#endif
}

void ESPWiFi::streamRSSI() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
  if (!s_rssiStarted) {
    return;
  }
  if (s_rssiWs.numClients() == 0) {
    return; // No one is listening; do no work.
  }

  constexpr int kIntervalMs = 500; // smooth UI without spamming
  constexpr int kForceMs = 2000;   // keep-alive update even if RSSI is stable
  constexpr int kMinDeltaDbm = 1;  // suppress identical repeats

  // runSystem() ticks every ~10ms. Use IntervalTimer to keep this bounded and
  // avoid doing work every loop iteration.
  static IntervalTimer pollTimer(kIntervalMs);
  static IntervalTimer keepAliveTimer(kForceMs);
  static int lastSentRssi = 0x7fffffff;

  const int64_t nowUs = esp_timer_get_time();
  if (!pollTimer.shouldRunAt(nowUs)) {
    return;
  }

  // Only valid in STA mode when connected.
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
    return;
  }

  const int rssi = ap.rssi;
  const bool changed = (lastSentRssi == 0x7fffffff) ||
                       (abs(rssi - lastSentRssi) >= kMinDeltaDbm);
  const bool forced = keepAliveTimer.shouldRunAt(nowUs);
  if (!changed && !forced) {
    return;
  }

  // Send as a tiny text frame containing just the integer (dashboard expects
  // this).
  char msg[8];
  int n = snprintf(msg, sizeof(msg), "%d", rssi);
  if (n <= 0) {
    return;
  }

  (void)s_rssiWs.textAll(msg, (size_t)n);
  lastSentRssi = rssi;
  keepAliveTimer.resetAt(nowUs); // extend keep-alive after any successful send
#endif
}
