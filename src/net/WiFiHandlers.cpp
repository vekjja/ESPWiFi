#ifndef ESPWiFi_WIFI_HANDLERS
#define ESPWiFi_WIFI_HANDLERS

#include "ESPWiFi.h"
#include "esp_netif.h"
#include "esp_wifi.h"

// ========== WiFi Event Handler Infrastructure ==========

esp_err_t ESPWiFi::registerWiFiHandlers() {
  // Already registered?
  if (wifi_event_instance != nullptr || ip_event_instance != nullptr) {
    return ESP_OK;
  }

  esp_err_t err;

  // Event when STA starts / disconnects / etc.
  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &ESPWiFi::wifiEventHandlerStatic,
                                            this, &wifi_event_instance);
  if (err != ESP_OK) {
    wifi_event_instance = nullptr;
    return err;
  }

  // Event when STA gets an IP
  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &ESPWiFi::ipEventHandlerStatic,
                                            this, &ip_event_instance);
  if (err != ESP_OK) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_event_instance);
    wifi_event_instance = nullptr;
    ip_event_instance = nullptr;
    return err;
  }

  // Create semaphore lazily
  if (wifi_connect_semaphore == nullptr) {
    wifi_connect_semaphore = xSemaphoreCreateBinary();
  }

  return ESP_OK;
}

void ESPWiFi::unregisterWiFiHandlers() {
  if (wifi_event_instance != nullptr) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          wifi_event_instance);
    wifi_event_instance = nullptr;
  }
  if (ip_event_instance != nullptr) {
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                          ip_event_instance);
    ip_event_instance = nullptr;
  }
}

// Static wrappers -> forward to instance methods
void ESPWiFi::wifiEventHandlerStatic(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data) {
  ESPWiFi *self = static_cast<ESPWiFi *>(arg);
  if (self) {
    self->wifiEventHandler(event_base, event_id, event_data);
  }
}

void ESPWiFi::ipEventHandlerStatic(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data) {
  ESPWiFi *self = static_cast<ESPWiFi *>(arg);
  if (self) {
    self->ipEventHandler(event_base, event_id, event_data);
  }
}

// Core WiFi event handler (member function)
void ESPWiFi::wifiEventHandler(esp_event_base_t event_base, int32_t event_id,
                               void *event_data) {
  if (event_base != WIFI_EVENT)
    return;

  switch (event_id) {
  case WIFI_EVENT_STA_START:
    // Station started; we usually call esp_wifi_connect() explicitly in
    // startClient()
    log(INFO, "📶 WiFi Started");
    break;

  case WIFI_EVENT_STA_DISCONNECTED: {
    wifi_event_sta_disconnected_t *disc =
        static_cast<wifi_event_sta_disconnected_t *>(event_data);

    wifi_connection_success = false;
    log(WARNING, "📶 WiFi Disconnected: %d", disc->reason);

    // Wake up any waiters
    if (wifi_connect_semaphore) {
      xSemaphoreGive(wifi_connect_semaphore);
    }

    // Auto-reconnect logic
    if (wifiAutoReconnect) {
      log(INFO, "🔄 📶 WiFi Auto Reconnect");
      esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) {
        log(ERROR, "esp_wifi_connect auto-reconnect failed: %s",
            esp_err_to_name(err));
      }
    }
    break;
  }

  default:
    // Other WiFi events are ignored for now
    break;
  }
}

// IP event handler (member function)
void ESPWiFi::ipEventHandler(esp_event_base_t event_base, int32_t event_id,
                             void *event_data) {
  if (event_base != IP_EVENT)
    return;

  switch (event_id) {
  case IP_EVENT_STA_GOT_IP: {
    wifi_connection_success = true;
    log(INFO, "📶 WiFi Connected: %s 🔗", ipAddress().c_str());

    // Notify waiters
    if (wifi_connect_semaphore) {
      xSemaphoreGive(wifi_connect_semaphore);
    }
    break;
  }

  default:
    break;
  }
}

// Optional helper to block until connected or timeout
bool ESPWiFi::waitForWiFiConnection(int timeout_ms, int check_interval_ms) {
  (void)check_interval_ms; // not strictly needed if we just use the semaphore

  if (wifi_connect_semaphore == nullptr) {
    wifi_connect_semaphore = xSemaphoreCreateBinary();
  }

  // Drain any old signal
  xSemaphoreTake(wifi_connect_semaphore, 0);
  wifi_connection_success = false;

  // Wait for either GOT_IP or DISCONNECTED
  TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
  if (xSemaphoreTake(wifi_connect_semaphore, ticks) == pdTRUE) {
    return wifi_connection_success;
  }

  // Timeout
  log(WARNING, "WiFi Connection Timeout: %d ms", timeout_ms);
  return false;
}

#endif // ESPWiFi_WIFI_HANDLERS
