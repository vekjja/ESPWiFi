#ifndef ESPWiFi_WIFI
#define ESPWiFi_WIFI

#include "ESPWiFi.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>

// Shared event loop initialization state
static bool event_loop_initialized = false;
static esp_netif_t *current_netif = nullptr;

// mDNS support - requires managed component
// TODO: Enable once component manager downloads mdns component
// #include <mdns.h>
#define MDNS_ENABLED 0

void ESPWiFi::startWiFi() {

  if (!config["wifi"]["enabled"].as<bool>()) {
    log(INFO, "🛜  WiFi Disabled");
    return;
  }

  std::string mode = config["wifi"]["mode"].as<std::string>();
  toLowerCase(mode);
  if (mode == "client") {
    startClient();
  } else if (mode == "accessPoint" || mode == "ap") {
    startAP();
  } else {
    log(WARNING, "⚠️  Invalid Mode: %s", mode.c_str());
    config["wifi"]["mode"] = "accessPoint"; // Ensure mode is set to accesspoint
    startAP();
  }
}

void ESPWiFi::startClient() {

  std::string ssid = config["wifi"]["client"]["ssid"].as<std::string>();
  std::string password = config["wifi"]["client"]["password"].as<std::string>();

  if (ssid.empty()) {
    log(WARNING, "Warning: SSID: Cannot be empty, starting Access Point");
    config["wifi"]["mode"] = "accessPoint";
    startAP();
    return;
  }
  log(INFO, "🔗 Connecting to WiFi Network:");
  log(DEBUG, "\tSSID: %s", ssid.c_str());
  log(DEBUG, "\tPassword: **********");

  // Initialize event loop if not already done
  if (!event_loop_initialized) {
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      ESP_ERROR_CHECK(ret);
    }
    event_loop_initialized = true;
  }

  // Clean up any existing WiFi/netif before starting client mode
  if (current_netif != nullptr) {
    esp_netif_destroy(current_netif);
    current_netif = nullptr;
  }
  // Stop and deinit WiFi if it's running (ignore errors if not initialized)
  esp_wifi_stop();
  esp_err_t deinit_ret = esp_wifi_deinit();
  if (deinit_ret != ESP_OK && deinit_ret != ESP_ERR_INVALID_STATE) {
    // Only log error if it's something other than "not initialized"
    log(WARNING, "Warning: WiFi deinit returned: %s",
        esp_err_to_name(deinit_ret));
  }

  // Initialize network interface
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);
  current_netif = sta_netif;

  // Initialize WiFi
  wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  // Configure WiFi
  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.sta.ssid, ssid.c_str(),
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy((char *)wifi_config.sta.password, password.c_str(),
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // Get MAC address (after WiFi is initialized)
  uint8_t mac[6];
  esp_err_t mac_ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
  if (mac_ret == ESP_OK) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
             mac[1], mac[2], mac[3], mac[4], mac[5]);
    log(DEBUG, "\tMAC: %s", macStr);
  }
  printf("\t");

  // Wait for connection - check if we got an IP address
  int64_t start = esp_timer_get_time() / 1000; // Convert to milliseconds
  wifi_ap_record_t ap_info;
  bool connected = false;

  while ((esp_timer_get_time() / 1000) - start < connectTimeout) {
    if (connectSubroutine != nullptr) {
      connectSubroutine();
    }
    printf(".");
    fflush(stdout);

    // Check if we have an IP address (more reliable than ap_info)
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
      connected = true;
      // Also get AP info for RSSI and channel
      esp_wifi_sta_get_ap_info(&ap_info);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms instead of 30ms
  }
  printf("\n");

  if (!connected) {
    log(ERROR, "🛜 Failed to connect to WiFi");
    config["wifi"]["mode"] = "accessPoint";
    startAP();
    return;
  }

  log(INFO, "🛜  WiFi Connected");

  // Get hostname
  const char *hostname = nullptr;
  esp_err_t err = esp_netif_get_hostname(sta_netif, &hostname);
  if (err != ESP_OK || hostname == nullptr || strlen(hostname) == 0) {
    std::string deviceName = config["deviceName"].as<std::string>();
    hostname = deviceName.c_str();
  }
  log(DEBUG, "\tHostname: %s", hostname ? hostname : "N/A");

  // Get IP info
  esp_netif_ip_info_t ip_info;
  ESP_ERROR_CHECK(esp_netif_get_ip_info(sta_netif, &ip_info));
  char ip_str[16];
  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
  log(DEBUG, "\tIP Address: %s", ip_str);

  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.netmask));
  log(DEBUG, "\tSubnet: %s", ip_str);

  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.gw));
  log(DEBUG, "\tGateway: %s", ip_str);

  // Get DNS (primary)
  esp_netif_dns_info_t dns_info;
  if (esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) ==
      ESP_OK) {
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    log(DEBUG, "\tDNS: %s", ip_str);
  }

  log(DEBUG, "\tRSSI: %d dBm", ap_info.rssi);
  log(DEBUG, "\tChannel: %d", ap_info.primary);
}

int ESPWiFi::selectBestChannel() {
  // client count for each channel, 14 for 2.4 GHz band
  int channels[14] = {0};

  // Perform WiFi scan (only if WiFi is initialized)
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
    // If scan fails (e.g., WiFi not initialized), return default channel
    log(DEBUG, "WiFi scan not available, using default channel 1");
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
        if (channel > 0 &&
            channel <= 13) { // Ensure the channel is within a valid range
          channels[channel]++;
        }
      }
      free(ap_records);
    }
  }

  int leastCongestedChannel = 1; // Default to channel 1
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
  log(DEBUG, "\tSSID: %s", ssid.c_str());
  log(DEBUG, "\tPassword: %s", password.c_str());
  int bestChannel = selectBestChannel();
  log(DEBUG, "\tChannel: %d", bestChannel);

  // Initialize event loop if not already done
  if (!event_loop_initialized) {
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      ESP_ERROR_CHECK(ret);
    }
    event_loop_initialized = true;
  }

  // Clean up any existing WiFi/netif before starting AP mode
  if (current_netif != nullptr) {
    esp_netif_destroy(current_netif);
    current_netif = nullptr;
  }
  // Stop and deinit WiFi if it's running (ignore errors if not initialized)
  esp_wifi_stop();
  esp_err_t deinit_ret = esp_wifi_deinit();
  if (deinit_ret != ESP_OK && deinit_ret != ESP_ERR_INVALID_STATE) {
    // Only log error if it's something other than "not initialized"
    log(WARNING, "Warning: WiFi deinit returned: %s",
        esp_err_to_name(deinit_ret));
  }

  // Initialize network interface
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  assert(ap_netif);
  current_netif = ap_netif;

  // Initialize WiFi
  wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

  // Configure AP
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

  // Get IP address
  esp_netif_ip_info_t ip_info;
  ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip_info));

  if (ip_info.ip.addr == 0) {
    log(ERROR, "Failed to start Access Point");
    return;
  }

  char ip_str[16];
  snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
  log(DEBUG, "\tIP Address: %s", ip_str);

#ifdef LED_BUILTIN
  gpio_set_direction((gpio_num_t)LED_BUILTIN, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)LED_BUILTIN, 0); // Turn on LED to indicate AP mode
#endif
}

void ESPWiFi::startMDNS() {
  if (!config["wifi"]["enabled"].as<bool>()) {
    log(INFO, "🏷️  mDNS Disabled");
    return;
  }

#if MDNS_ENABLED
  std::string domain = config["deviceName"].as<std::string>();
  toLowerCase(domain);

  esp_err_t err = mdns_init();
  if (err != ESP_OK) {
    log(ERROR, "Error setting up MDNS responder!");
    return;
  }

  err = mdns_hostname_set(domain.c_str());
  if (err != ESP_OK) {
    log(ERROR, "Error setting MDNS hostname!");
    return;
  }

  err = mdns_instance_name_set(domain.c_str());
  if (err != ESP_OK) {
    log(ERROR, "Error setting MDNS instance name!");
    return;
  }

  err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
  if (err != ESP_OK) {
    log(ERROR, "Error adding MDNS service!");
    return;
  }

  log(INFO, "🏷️  mDNS Started");
  log(DEBUG, "\tDomain Name: %s.local", domain.c_str());
#else
  log(INFO, "🏷️  mDNS Disabled (component not available)");
  log(DEBUG, "\tNote: mDNS requires managed component. Add to "
             "idf_component.yml and rebuild.");
#endif
}

std::string ESPWiFi::getHostname() {
  std::string hostname = "espwifi-000000";
  // generate from MAC address
  uint8_t mac[6];
  esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
  if (ret == ESP_OK) {
    // Format hostname as "espwifi-XXXXXX" where XXXXXX is last 6 hex digits
    // of MAC
    char macSuffix[7];
    snprintf(macSuffix, sizeof(macSuffix), "%02x%02x%02x", mac[3], mac[4],
             mac[5]);
    hostname = "espwifi-" + std::string(macSuffix);
  }
  return hostname;
}

#endif // ESPWiFi_WIFI