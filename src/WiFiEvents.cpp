#include "ESPWiFi.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdio>

// Static callbacks for ESP-IDF event system
void ESPWiFi::wifi_event_handler_static(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data) {
  ESPWiFi *instance = static_cast<ESPWiFi *>(arg);
  if (instance != nullptr) {
    instance->wifi_event_handler(event_base, event_id, event_data);
  }
}

void ESPWiFi::ip_event_handler_static(void *arg, esp_event_base_t event_base,
                                      int32_t event_id, void *event_data) {
  ESPWiFi *instance = static_cast<ESPWiFi *>(arg);
  if (instance != nullptr) {
    instance->ip_event_handler(event_base, event_id, event_data);
  }
}

// WiFi event handler - handles connection/disconnection events
void ESPWiFi::wifi_event_handler(esp_event_base_t event_base, int32_t event_id,
                                 void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    wifi_event_sta_connected_t *event =
        (wifi_event_sta_connected_t *)event_data;
    // SSID is a uint8_t array, convert to string
    std::string ssid_str(reinterpret_cast<const char *>(event->ssid));

    ESP_LOGI("wifi", "🔗 Connected to AP SSID:%s channel:%d", ssid_str.c_str(),
             event->channel);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *event =
        (wifi_event_sta_disconnected_t *)event_data;
    // SSID is a uint8_t array, convert to string
    std::string ssid_str(reinterpret_cast<const char *>(event->ssid));
    ESP_LOGW("wifi", "⛓️‍💥 Disconnected from AP SSID:%s reason:%d",
             ssid_str.c_str(), event->reason);

    // Signal connection failure if we're waiting for connection
    if (wifi_connect_semaphore != nullptr) {
      wifi_connection_success = false;
      xSemaphoreGive(wifi_connect_semaphore);
    }
  }
}

// IP event handler - handles IP address assignment
void ESPWiFi::ip_event_handler(esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI("wifi", "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));

    // Signal connection success
    if (wifi_connect_semaphore != nullptr) {
      wifi_connection_success = true;
      xSemaphoreGive(wifi_connect_semaphore);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
    ESP_LOGW("wifi", "Lost IP address");

    // Signal connection failure if we're waiting for connection
    if (wifi_connect_semaphore != nullptr) {
      wifi_connection_success = false;
      xSemaphoreGive(wifi_connect_semaphore);
    }
  }
}

// Register WiFi event handlers for connection monitoring
esp_err_t ESPWiFi::registerWiFiHandlers() {
  // Create semaphore for connection signaling if it doesn't exist
  if (wifi_connect_semaphore == nullptr) {
    wifi_connect_semaphore = xSemaphoreCreateBinary();
    if (wifi_connect_semaphore == nullptr) {
      return ESP_ERR_NO_MEM;
    }
  } else {
    // Reset semaphore for new connection attempt
    xSemaphoreTake(wifi_connect_semaphore, 0); // Non-blocking take to clear
  }

  // Reset connection state
  wifi_connection_success = false;

  // Unregister existing handlers if they exist
  if (wifi_event_instance != nullptr) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_event_instance);
    wifi_event_instance = nullptr;
  }
  if (ip_event_instance != nullptr) {
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                          ip_event_instance);
    ip_event_instance = nullptr;
  }

  // Register event handlers for this connection attempt
  // Pass 'this' as the argument so static callbacks can access instance
  esp_err_t ret = esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &ESPWiFi::wifi_event_handler_static, this,
      &wifi_event_instance);
  if (ret != ESP_OK) {
    return ret;
  }

  ret = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                            &ESPWiFi::ip_event_handler_static,
                                            this, &ip_event_instance);
  if (ret != ESP_OK) {
    // Clean up WiFi event handler if IP handler registration fails
    if (wifi_event_instance != nullptr) {
      esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_instance);
      wifi_event_instance = nullptr;
    }
    return ret;
  }

  return ESP_OK;
}

// Unregister WiFi event handlers
void ESPWiFi::unregisterWiFiHandlers() {
  // Unregister event handlers
  if (wifi_event_instance != nullptr) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_event_instance);
    wifi_event_instance = nullptr;
  }
  if (ip_event_instance != nullptr) {
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
                                          ip_event_instance);
    ip_event_instance = nullptr;
  }
}

// Wait for WiFi connection result
bool ESPWiFi::waitForWiFiConnection(int timeout_ms, int check_interval_ms) {
  if (wifi_connect_semaphore == nullptr) {
    return false;
  }

  int64_t start_time = esp_timer_get_time() / 1000; // Convert to milliseconds
  const TickType_t check_interval = pdMS_TO_TICKS(check_interval_ms);

  while (true) {
    // Check if timeout has elapsed
    int64_t elapsed = (esp_timer_get_time() / 1000) - start_time;
    if (elapsed >= timeout_ms) {
      return false;
    }

    // Call connectSubroutine if provided
    if (connectSubroutine != nullptr) {
      connectSubroutine();
    }

    // Print progress indicator
    printf(".");
    fflush(stdout);

    // Try to take semaphore with short timeout (non-blocking check)
    if (xSemaphoreTake(wifi_connect_semaphore, check_interval) == pdTRUE) {
      // Semaphore was given - connection event occurred
      return wifi_connection_success;
    }
  }
}
