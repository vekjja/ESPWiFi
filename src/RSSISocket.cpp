// RSSISocket.cpp - Stubbed for now
#include "ESPWiFi.h"
#include "esp_wifi.h"

#ifdef CONFIG_HTTPD_WS_SUPPORT
// RSSI WS is now an ESPWiFi member (rssiSoc/rssiSocStarted) so we can report
// cloud tunnel state in /api/info and apply config changes live.
#endif

void ESPWiFi::startRSSIWebSocket() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  log(WARNING, "📶 RSSI WebSocket disabled (CONFIG_HTTPD_WS_SUPPORT is off)");
  return;
#else
  if (rssiSocStarted) {
    return;
  }

  // Keep handler light: we don't expect inbound frames; we only stream RSSI
  // out. Small max message len keeps RX buffer tiny.
  rssiSocStarted = rssiSoc.begin("/ws/rssi", this,
                                 /*onMessage*/ nullptr,
                                 /*onConnect*/ nullptr,
                                 /*onDisconnect*/ nullptr,
                                 /*maxMessageLen*/ 512,
                                 /*maxBroadcastLen*/ 32,
                                 /*requireAuth*/ false);

  if (!rssiSocStarted) {
    log(ERROR, "📶 RSSI WebSocket failed to start");
    return;
  }

  // Apply cloud tunnel config immediately after creating the endpoint so it can
  // connect without requiring a reboot.
  rssiSoc.syncCloudTunnelFromConfig();
#endif
}

void ESPWiFi::streamRSSI() {
#ifndef CONFIG_HTTPD_WS_SUPPORT
  return;
#else
  if (!rssiSocStarted) {
    return;
  }
  // Only stream when there's a real consumer: LAN clients or cloud UI attached.
  if (rssiSoc.numLanClients() == 0 && !rssiSoc.cloudUIConnected()) {
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

  (void)rssiSoc.textAll(msg, (size_t)n);
  lastSentRssi = rssi;
  keepAliveTimer.resetAt(nowUs); // extend keep-alive after any successful send
#endif
}
