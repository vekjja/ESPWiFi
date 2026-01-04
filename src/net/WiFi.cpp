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
#include <cstdlib>
#include <cstring>
#include <string>

// Shared event loop / WiFi driver state (single definition)
static bool event_loop_initialized = false;
static bool netif_initialized = false;
static bool wifi_initialized = false;
static esp_netif_t *current_netif = nullptr;

bool ESPWiFi::isWiFiInitialized() const { return wifi_initialized; }

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

void ESPWiFi::startWiFi() {
  if (!config["wifi"]["enabled"].as<bool>()) {
    log(INFO, "📶 WiFi Disabled");
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
  log(DEBUG, "📶\tSSID: %s", ssid.c_str());
  log(DEBUG, "📶\tPassword: **********");

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

  setHostname(config["deviceName"].as<std::string>());

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

  // (Alternative: you could use waitForWiFiConnection(connectTimeout) instead)
  // bool connected = waitForWiFiConnection(connectTimeout);

  if (!connected) {
    log(ERROR, "📶 Failed to connect to WiFi, falling back to AP");
    setWiFiAutoReconnect(false); // Disable reconnect when switching to AP
    config["wifi"]["mode"] = "accessPoint";
    startAP(); // This will start BLE if enabled in config
    return;
  }

  // WiFi connected successfully - stop BLE provisioning if running
#ifdef CONFIG_BT_NIMBLE_ENABLED
  deinitBLE();
#endif

  std::string hostname = getHostname();
  log(DEBUG, "📶\tHostname: %s", hostname.c_str());

  esp_netif_ip_info_t ip_info;
  ESP_ERROR_CHECK(esp_netif_get_ip_info(current_netif, &ip_info));

  char ip_str[16];
  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.netmask));
  log(DEBUG, "📶\tSubnet: %s", ip_str);

  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.gw));
  log(DEBUG, "📶\tGateway: %s", ip_str);

  esp_netif_dns_info_t dns_info;
  if (esp_netif_get_dns_info(current_netif, ESP_NETIF_DNS_MAIN, &dns_info) ==
      ESP_OK) {
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    log(DEBUG, "📶\tDNS: %s", ip_str);
  }

  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    log(DEBUG, "📶\tRSSI: %d dBm", ap_info.rssi);
    log(DEBUG, "📶\tChannel: %d", ap_info.primary);
  }
}

int ESPWiFi::selectBestChannel() {
  // Count clients per channel (14 channels in 2.4 GHz band)
  int channels[14] = {0};

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
  esp_wifi_scan_get_ap_num(&numNetworks);

  if (numNetworks > 0) {
    wifi_ap_record_t *ap_records =
        (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * numNetworks);
    if (ap_records != nullptr) {
      ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&numNetworks, ap_records));

      for (int i = 0; i < numNetworks; i++) {
        int channel = ap_records[i].primary;
        if (channel > 0 && channel <= 13) {
          channels[channel]++;
        }
      }
      free(ap_records);
    }
  }

  int leastCongestedChannel = 1;
  for (int i = 1; i <= 13; i++) {
    if (channels[i] < channels[leastCongestedChannel]) {
      leastCongestedChannel = i;
    }
  }
  return leastCongestedChannel;
}

void ESPWiFi::startAP() {
  std::string ssid = config["wifi"]["accessPoint"]["ssid"].as<std::string>();
  std::string password =
      config["wifi"]["accessPoint"]["password"].as<std::string>();

  log(INFO, "📡 Starting Access Point");
  log(DEBUG, "📶\tSSID: %s", ssid.c_str());
  log(DEBUG, "📶\tPassword: %s", password.c_str());

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
  log(DEBUG, "📶\tChannel: %d", bestChannel);

  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  assert(ap_netif);
  current_netif = ap_netif;

  setHostname(config["deviceName"].as<std::string>());

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
  log(DEBUG, "📶\tIP Address: %s", ip_str);

  // Start BLE provisioning when in AP mode (if enabled in config)
  // MUST be done after WiFi is fully initialized to avoid BT/WiFi coexistence
  // issues
#ifdef CONFIG_BT_NIMBLE_ENABLED
  if (config["ble"]["enabled"].as<bool>()) {
    // Give WiFi a moment to stabilize before enabling BLE
    vTaskDelay(pdMS_TO_TICKS(200));
    startBLE();
  }
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
  // Attempt to get hostname from network interface
  if (current_netif != nullptr) {
    const char *hostname_ptr = nullptr;
    esp_err_t err = esp_netif_get_hostname(current_netif, &hostname_ptr);
    if (err == ESP_OK && hostname_ptr != nullptr && strlen(hostname_ptr) > 0) {
      config["hostname"] = std::string(hostname_ptr);
      return std::string(hostname_ptr);
    }
  }

  // Fallback: "espwifi-XXXXXX" where XXXXXX is last 6 hex digits of MAC
  uint8_t mac[6];
  esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (ret == ESP_OK) {
    char macSuffix[7];
    snprintf(macSuffix, sizeof(macSuffix), "%02x%02x%02x", mac[3], mac[4],
             mac[5]);
    config["hostname"] = "espwifi-" + std::string(macSuffix);
    return "espwifi-" + std::string(macSuffix);
  }

  return "espwifi-000000";
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
