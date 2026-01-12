#include "ESPWiFi.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

// Shared event loop / WiFi driver state (single definition)
static bool event_loop_initialized = false;
static bool netif_initialized = false;
static bool wifi_initialized = false;
static esp_netif_t *current_netif = nullptr;

bool ESPWiFi::isWiFiInitialized() const { return wifi_initialized; }

void ESPWiFi::wifiConfigHandler(const JsonDocument &oldConfig) {
  // Determine whether WiFi needs a restart based on changed settings.
  //
  // Convention: Compare oldConfig (before) vs config (after update).
  // We only *schedule* a restart here; handleConfigUpdate() performs it
  // after all handlers run, so startWiFi() uses the new config.
  bool wifiNeedsRestart = false;

  const bool oldEnabled = oldConfig["wifi"]["enabled"].as<bool>();
  const bool newEnabled = config["wifi"]["enabled"].as<bool>();
  if (oldEnabled != newEnabled) {
    log(DEBUG, "📶 WiFi enabled changed: %d -> %d", oldEnabled, newEnabled);
    wifiNeedsRestart = true;
  }

  std::string oldMode = oldConfig["wifi"]["mode"].as<std::string>();
  std::string newMode = config["wifi"]["mode"].as<std::string>();
  toLowerCase(oldMode);
  toLowerCase(newMode);
  if (oldMode != newMode) {
    log(DEBUG, "📶 WiFi mode changed: %s -> %s", oldMode.c_str(),
        newMode.c_str());
    wifiNeedsRestart = true;
  }

  std::string oldSsid = oldConfig["wifi"]["client"]["ssid"].as<std::string>();
  std::string newSsid = config["wifi"]["client"]["ssid"].as<std::string>();
  if (oldSsid != newSsid) {
    log(DEBUG, "📶 WiFi SSID changed: '%s' -> '%s'", oldSsid.c_str(),
        newSsid.c_str());
    wifiNeedsRestart = true;
  }

  std::string oldPw = oldConfig["wifi"]["client"]["password"].as<std::string>();
  std::string newPw = config["wifi"]["client"]["password"].as<std::string>();
  if (oldPw != newPw) {
    wifiNeedsRestart = true;
  }

  if (wifiNeedsRestart) {
    wifiRestartRequested_ = true;
    log(INFO, "📶 WiFi config changed, restart requested");

    // Perform the restart immediately
    log(INFO, "📶 WiFi config changed; restarting WiFi");
    stopWiFi();                     // Properly stop before restart
    vTaskDelay(pdMS_TO_TICKS(200)); // Let everything settle
    startWiFi();
  }
}

void ESPWiFi::initNVS() {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

void ESPWiFi::stopWiFi() {
  if (!wifi_initialized) {
    return; // Nothing to stop
  }

  log(DEBUG, "📶 Stopping WiFi...");

  // Stop WiFi driver
  esp_err_t ret = esp_wifi_stop();
  if (ret != ESP_OK) {
    log(WARNING, "📶 WiFi stop failed: %s", esp_err_to_name(ret));
  }
  vTaskDelay(pdMS_TO_TICKS(100)); // Let it fully stop

  // Deinitialize WiFi
  ret = esp_wifi_deinit();
  if (ret != ESP_OK) {
    log(WARNING, "📶 WiFi deinit failed: %s", esp_err_to_name(ret));
  }
  wifi_initialized = false;
  vTaskDelay(pdMS_TO_TICKS(100));

  // Destroy network interface to allow mode switch
  if (current_netif != nullptr) {
    esp_netif_destroy(current_netif);
    current_netif = nullptr;
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  log(DEBUG, "📶 WiFi stopped");
}

void ESPWiFi::startWiFi() {
  if (!config["wifi"]["enabled"].as<bool>()) {
    log(INFO, "📶 WiFi Disabled");
#ifdef CONFIG_BT_NIMBLE_ENABLED
    // For now, BLE should start every boot (even if WiFi is disabled) so the UI
    // can always pair / provision.
    startBLE();
#endif
    return;
  }
  initNVS();

  std::string mode = config["wifi"]["mode"].as<std::string>();
  toLowerCase(mode);
  if (mode == "client") {
    startClient();
  } else if (mode == "accesspoint" || mode == "ap") {
    startAP();
  } else {
    log(WARNING, "Invalid Mode: %s", mode.c_str());
    config["wifi"]["mode"] = "accessPoint";
    startAP();
  }
}

void ESPWiFi::startClient() {

  std::string ssid = config["wifi"]["client"]["ssid"].as<std::string>();
  std::string password = config["wifi"]["client"]["password"].as<std::string>();

  if (ssid.empty()) {
    log(WARNING, "Warning: SSID cannot be empty, starting Access Point");
    config["wifi"]["mode"] = "accessPoint";
    startAP();
    return;
  }

  log(INFO, "📶 WiFi Connecting to Network");
  log(INFO, "📶\tSSID: %s", ssid.c_str());
  log(INFO, "📶\tPassword: **********");

  if (!event_loop_initialized) {
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      ESP_ERROR_CHECK(ret);
    }
    event_loop_initialized = true;
  }

  if (!netif_initialized) {
    ESP_ERROR_CHECK(esp_netif_init());
    netif_initialized = true;
  }

  ESP_ERROR_CHECK(registerWiFiHandlers());
  setWiFiAutoReconnect(true);

  if (current_netif != nullptr) {
    esp_netif_destroy(current_netif);
    current_netif = nullptr;
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (wifi_initialized) {
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_initialized = false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);
  current_netif = sta_netif;

  wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));
  wifi_initialized = true;

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  setHostname(genHostname());

  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.sta.ssid, ssid.c_str(),
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, password.c_str(),
          sizeof(wifi_config.sta.password) - 1);

  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  vTaskDelay(pdMS_TO_TICKS(100)); // Let driver settle

  // Apply power management settings after WiFi start
  // Note: esp_wifi_set_max_tx_power() requires WiFi to be started
  applyWiFiPowerSettings();

  ESP_ERROR_CHECK(esp_wifi_disconnect()); // Clear any stale connection state
  vTaskDelay(pdMS_TO_TICKS(50));
  ESP_ERROR_CHECK(esp_wifi_connect());

  bool connected = false;
  int64_t start_time_ms = esp_timer_get_time() / 1000;
  while ((esp_timer_get_time() / 1000) - start_time_ms < connectTimeout) {
    if (connectSubroutine != nullptr) {
      connectSubroutine();
    }

    // printf(".");
    // fflush(stdout);

    if (current_netif != nullptr) {
      esp_netif_ip_info_t ip_info;
      if (esp_netif_get_ip_info(current_netif, &ip_info) == ESP_OK) {
        if (ip_info.ip.addr != 0) {
          connected = true;
          break;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(30));
  }
  printf("\n");

  if (!connected) {
    log(ERROR, "📶 Failed to connect to WiFi, falling back to AP");
    setWiFiAutoReconnect(false); // Disable reconnect when switching to AP
    config["wifi"]["mode"] = "accessPoint";
    startAP(); // This will start BLE if enabled in config
    return;
  }

  // WiFi connected successfully.
  // For now, BLE should start every boot (client or AP) so the UI can always
  // pair / provision / enable cloud tunnel.
#ifdef CONFIG_BT_NIMBLE_ENABLED
  // Give WiFi a moment to stabilize before enabling BLE (coexistence).
  vTaskDelay(pdMS_TO_TICKS(200));
  startBLE();
#endif

  std::string hostname = getHostname();
  log(INFO, "📶\tHostname: %s", hostname.c_str());

  esp_netif_ip_info_t ip_info;
  ESP_ERROR_CHECK(esp_netif_get_ip_info(current_netif, &ip_info));

  char ip_str[16];
  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.netmask));
  log(INFO, "📶\tSubnet: %s", ip_str);

  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.gw));
  log(INFO, "📶\tGateway: %s", ip_str);

  esp_netif_dns_info_t dns_info;
  if (esp_netif_get_dns_info(current_netif, ESP_NETIF_DNS_MAIN, &dns_info) ==
      ESP_OK) {
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    log(INFO, "📶\tDNS: %s", ip_str);
  }

  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    log(INFO, "📶\tRSSI: %d dBm", ap_info.rssi);
    log(INFO, "📶\tChannel: %d", ap_info.primary);
  }
}

int ESPWiFi::selectBestChannel() {
  // Select the "best" 2.4GHz channel for our AP.
  //
  // Note: Many client devices (notably some macOS/iOS regional configs) will
  // fail to join or will "timeout" when an AP is on channels 12/13. To keep
  // provisioning reliable across regions, we constrain to channels 1-11.
  //
  // Scoring model:
  // - Stronger nearby APs (higher RSSI) contribute more interference.
  // - Adjacent channels partially overlap, so they also contribute.
  // - If scores tie (or are extremely close), prefer 1/6/11.
  constexpr int kMinChannel = 1;
  constexpr int kMaxChannel = 11;
  constexpr int kMaxOverlapDistance =
      4; // beyond this, assume negligible overlap

  float score[14] = {0.0f}; // index by channel number; [0] unused

  wifi_scan_config_t scan_config = {};
  scan_config.ssid = nullptr;
  scan_config.bssid = nullptr;
  scan_config.channel = 0;
  scan_config.show_hidden = false;
  scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan_config.scan_time.active.min = 100;
  scan_config.scan_time.active.max = 300;

  esp_err_t scan_ret = esp_wifi_scan_start(&scan_config, true);
  if (scan_ret != ESP_OK) {
    log(WARNING, " WiFi scan not available, using default channel 1");
    return 1;
  }

  uint16_t numNetworks = 0;
  (void)esp_wifi_scan_get_ap_num(&numNetworks);

  if (numNetworks > 0) {
    wifi_ap_record_t *ap_records =
        (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * numNetworks);
    if (ap_records != nullptr) {
      esp_err_t rec_ret =
          esp_wifi_scan_get_ap_records(&numNetworks, ap_records);
      if (rec_ret != ESP_OK) {
        log(WARNING,
            " WiFi scan results unavailable (%s), using default channel 1",
            esp_err_to_name(rec_ret));
        free(ap_records);
        return 1;
      }

      auto rssiToWeight = [](int rssi) -> float {
        // Map RSSI dBm to a bounded weight. Typical RSSI range is ~[-90, -30].
        // -90 dBm -> ~1, -80 -> 2, -70 -> 4, ... -40 -> 32, -30 -> 64
        float w = powf(2.0f, (static_cast<float>(rssi) + 90.0f) / 10.0f);
        if (w < 0.25f)
          w = 0.25f;
        if (w > 64.0f)
          w = 64.0f;
        return w;
      };

      auto overlapWeight = [](int distance) -> float {
        // distance 0 => 1.0, 1 => 0.5, 2 => 0.25, ...
        if (distance < 0)
          distance = -distance;
        if (distance > kMaxOverlapDistance)
          return 0.0f;
        return 1.0f / static_cast<float>(1 << distance);
      };

      for (int i = 0; i < numNetworks; i++) {
        int apChannel = ap_records[i].primary;
        if (apChannel < kMinChannel || apChannel > kMaxChannel) {
          continue;
        }

        float apWeight = rssiToWeight(ap_records[i].rssi);
        for (int ch = kMinChannel; ch <= kMaxChannel; ch++) {
          int d = ch - apChannel;
          if (d < 0)
            d = -d;
          float ow = overlapWeight(d);
          if (ow > 0.0f) {
            score[ch] += apWeight * ow;
          }
        }
      }
      free(ap_records);
    }
  }

  auto isPreferred = [](int ch) -> bool {
    return (ch == 1 || ch == 6 || ch == 11);
  };

  auto preferredOrder = [](int ch) -> int {
    // Lower is better among preferred channels
    if (ch == 1)
      return 0;
    if (ch == 6)
      return 1;
    if (ch == 11)
      return 2;
    return 999;
  };

  constexpr float kTieEpsilon = 0.001f;
  int bestChannel = 1;
  for (int ch = kMinChannel; ch <= kMaxChannel; ch++) {
    if (score[ch] + kTieEpsilon < score[bestChannel]) {
      bestChannel = ch;
      continue;
    }

    float diff = score[ch] - score[bestChannel];
    if (diff < 0.0f)
      diff = -diff;
    if (diff <= kTieEpsilon) {
      // Tie-break: prefer 1/6/11; then stable ordering.
      bool chPref = isPreferred(ch);
      bool bestPref = isPreferred(bestChannel);
      if (chPref && !bestPref) {
        bestChannel = ch;
      } else if (chPref && bestPref) {
        if (preferredOrder(ch) < preferredOrder(bestChannel)) {
          bestChannel = ch;
        }
      } else if (!chPref && !bestPref) {
        if (ch < bestChannel) {
          bestChannel = ch;
        }
      }
    }
  }

  log(INFO, "📶\tChannel scan: selected %d (score=%.2f)", bestChannel,
      score[bestChannel]);
  return bestChannel;
}

void ESPWiFi::startAP() {
  // std::string ssid = genHostname();
  std::string ssid = config["wifi"]["accessPoint"]["ssid"].as<std::string>();
  std::string password =
      config["wifi"]["accessPoint"]["password"].as<std::string>();

  log(INFO, "📡 Starting Access Point");
  log(INFO, "📶\tSSID: %s", ssid.c_str());
  log(INFO, "📶\tPassword: %s", password.c_str());

  setWiFiAutoReconnect(false); // No STA auto-reconnect in AP mode

  if (!event_loop_initialized) {
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      ESP_ERROR_CHECK(ret);
    }
    event_loop_initialized = true;
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (!netif_initialized) {
    ESP_ERROR_CHECK(esp_netif_init());
    netif_initialized = true;
  }

  // Clean up existing netif
  if (current_netif != nullptr) {
    esp_netif_destroy(current_netif);
    current_netif = nullptr;
  }

  // Stop + deinit WiFi driver before reconfiguring
  if (wifi_initialized) {
    esp_wifi_stop();
    (void)esp_wifi_deinit();
    wifi_initialized = false;
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  int bestChannel =
      selectBestChannel(); // Falls back to channel 1 if scan fails
  // Defensive clamp (selectBestChannel() already constrains to 1-11)
  if (bestChannel < 1)
    bestChannel = 1;
  if (bestChannel > 11)
    bestChannel = 11;
  log(INFO, "📶\tChannel: %d", bestChannel);

  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  assert(ap_netif);
  current_netif = ap_netif;

  setHostname(genHostname());

  wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));
  wifi_initialized = true;

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.ap.ssid, ssid.c_str(),
          sizeof(wifi_config.ap.ssid) - 1);
  strncpy((char *)wifi_config.ap.password, password.c_str(),
          sizeof(wifi_config.ap.password) - 1);

  wifi_config.ap.ssid_len = strlen((char *)wifi_config.ap.ssid);
  wifi_config.ap.channel = bestChannel;
  wifi_config.ap.authmode =
      password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  wifi_config.ap.max_connection = 4;
  wifi_config.ap.beacon_interval = 100;

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // Apply power management settings after WiFi start
  // Note: esp_wifi_set_max_tx_power() requires WiFi to be started
  applyWiFiPowerSettings();

  esp_netif_ip_info_t ip_info;
  ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip_info));

  if (ip_info.ip.addr == 0) {
    log(ERROR, "Failed to start Access Point");
    return;
  }

  char ip_str[16];
  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
  log(INFO, "📶\tIP Address: %s", ip_str);

  // Start BLE provisioning when in AP mode (if enabled in config)
  // MUST be done after WiFi is fully initialized to avoid BT/WiFi coexistence
  // issues
#ifdef CONFIG_BT_NIMBLE_ENABLED
  // For now, BLE should start every boot (AP mode included) so the UI can
  // always pair / provision.
  vTaskDelay(pdMS_TO_TICKS(200));
  startBLE();
#endif

#ifdef LED_BUILTIN
  gpio_set_direction((gpio_num_t)LED_BUILTIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)LED_BUILTIN, 0); // Turn on LED to indicate AP mode
#endif
}

std::string ESPWiFi::ipAddress() {
  if (current_netif == nullptr) {
    return "0.0.0.0";
  }

  esp_netif_ip_info_t ip_info;
  esp_err_t err = esp_netif_get_ip_info(current_netif, &ip_info);
  if (err != ESP_OK) {
    return "0.0.0.0";
  }

  char ip_str[16];
  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
  return std::string(ip_str);
}

std::string ESPWiFi::getHostname() {
  std::string hostname;

  // // Attempt to get hostname from network interface
  if (current_netif != nullptr) {
    const char *hostname_ptr = nullptr;
    esp_err_t err = esp_netif_get_hostname(current_netif, &hostname_ptr);
    if (err == ESP_OK && hostname_ptr != nullptr && strlen(hostname_ptr) > 0) {
      hostname = std::string(hostname_ptr);
      config["hostname"] = hostname;
    }
  }

  // // "deviceName-XXXXXX" where XXXXXX is last 6 hex digits of MAC
  // uint8_t mac[6];
  // esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  // if (ret == ESP_OK) {
  //   char macSuffix[7];
  //   snprintf(macSuffix, sizeof(macSuffix), "%02x%02x%02x", mac[3], mac[4],
  //            mac[5]);
  //   hostname =
  //       config["deviceName"].as<std::string>() + "-" +
  //       std::string(macSuffix);
  //   config["hostname"] = hostname;
  //   return hostname;
  // }

  return hostname;
}

std::string ESPWiFi::genHostname() {
  // // Attempt to get hostname from network interface
  // if (current_netif != nullptr) {
  //   const char *hostname_ptr = nullptr;
  //   esp_err_t err = esp_netif_get_hostname(current_netif, &hostname_ptr);
  //   if (err == ESP_OK && hostname_ptr != nullptr && strlen(hostname_ptr) > 0)
  //   {
  //     config["hostname"] = std::string(hostname_ptr);
  //     return std::string(hostname_ptr);
  //   }
  // }

  std::string hostname;
  // "deviceName-XXXXXX" where XXXXXX is last 6 hex digits of MAC
  uint8_t mac[6];
  esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (ret == ESP_OK) {
    char macSuffix[7];
    snprintf(macSuffix, sizeof(macSuffix), "%02x%02x%02x", mac[3], mac[4],
             mac[5]);
    hostname =
        config["deviceName"].as<std::string>() + "-" + std::string(macSuffix);
    config["hostname"] = hostname;
    return hostname;
  }

  return hostname;
}

void ESPWiFi::setHostname(std::string hostname) {
  if (current_netif == nullptr) {
    log(WARNING, "📶 Cannot set hostname: network interface not initialized");
    return;
  }

  if (hostname.empty()) {
    log(WARNING, "📶  Cannot set new hostname: hostname provided is empty");
    return;
  }

  toLowerCase(hostname);

  esp_err_t hostname_ret =
      esp_netif_set_hostname(current_netif, hostname.c_str());
  if (hostname_ret == ESP_OK) {
    config["hostname"] = hostname;
  } else {
    log(WARNING, "📶  Failed to set hostname: %s",
        esp_err_to_name(hostname_ret));
  }
}
