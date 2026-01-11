#include <CloudTunnel.h>

#include <cctype>
#include <cstring>
#include <string>

#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_timer.h"

// ============================================================================
// Helper functions
// ============================================================================

static size_t safeCopy(char *dst, size_t dstLen, const char *src) {
  if (dst == nullptr || dstLen == 0) {
    return 0;
  }
  dst[0] = '\0';
  if (src == nullptr) {
    return 0;
  }
  size_t i = 0;
  for (; i + 1 < dstLen && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return i;
}

static size_t safeAppend(char *dst, size_t dstLen, const char *src) {
  if (dst == nullptr || dstLen == 0) {
    return 0;
  }
  if (src == nullptr || src[0] == '\0') {
    return strlen(dst);
  }
  size_t di = strnlen(dst, dstLen);
  if (di >= dstLen) {
    dst[dstLen - 1] = '\0';
    return dstLen - 1;
  }
  size_t si = 0;
  while (di + 1 < dstLen && src[si] != '\0') {
    dst[di++] = src[si++];
  }
  dst[di] = '\0';
  return di;
}

static bool isUnreservedUrlChar(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return true;
  }
  return (c == '-' || c == '_' || c == '.' || c == '~');
}

static size_t urlEncode(const char *in, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return 0;
  }
  out[0] = '\0';
  if (in == nullptr) {
    return 0;
  }
  static const char *hex = "0123456789ABCDEF";
  size_t oi = 0;
  for (size_t i = 0; in[i] != '\0'; i++) {
    const unsigned char c = (unsigned char)in[i];
    if (isUnreservedUrlChar((char)c)) {
      if (oi + 1 >= outLen) {
        break;
      }
      out[oi++] = (char)c;
    } else {
      if (oi + 3 >= outLen) {
        break;
      }
      out[oi++] = '%';
      out[oi++] = hex[c >> 4];
      out[oi++] = hex[c & 0x0F];
    }
  }
  out[oi] = '\0';
  return oi;
}

// Extract a JSON string field from a small broker message
static bool jsonGetStringField(const char *json, size_t jsonLen,
                               const char *key, char *out, size_t outLen) {
  if (out == nullptr || outLen == 0) {
    return false;
  }
  out[0] = '\0';
  if (json == nullptr || key == nullptr || key[0] == '\0') {
    return false;
  }
  char needle[64];
  const size_t keyLen = strnlen(key, sizeof(needle));
  if (keyLen + 3 > sizeof(needle)) {
    return false;
  }
  needle[0] = '"';
  memcpy(&needle[1], key, keyLen);
  needle[1 + keyLen] = '"';
  needle[2 + keyLen] = '\0';

  const char *hay = json;
  const char *end = json + jsonLen;
  const size_t needleLen = strlen(needle);
  if (needleLen == 0) {
    return false;
  }
  for (const char *p = hay; p + needleLen < end; p++) {
    if (memcmp(p, needle, needleLen) != 0) {
      continue;
    }
    p += needleLen;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
      p++;
    }
    if (p >= end || *p != ':') {
      continue;
    }
    p++;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
      p++;
    }
    if (p >= end || *p != '"') {
      continue;
    }
    p++;
    size_t oi = 0;
    while (p < end && *p != '"' && oi + 1 < outLen) {
      const char c = *p++;
      if (c == '\\') {
        if (p < end) {
          out[oi++] = *p++;
        }
      } else {
        out[oi++] = c;
      }
    }
    out[oi] = '\0';
    return (oi > 0);
  }
  return false;
}

// Global dial mutex to serialize TLS handshakes
static SemaphoreHandle_t s_cloudDialMu = nullptr;
static portMUX_TYPE s_cloudDialMux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t cloudDialMu() {
  if (s_cloudDialMu == nullptr) {
    portENTER_CRITICAL(&s_cloudDialMux);
    if (s_cloudDialMu == nullptr) {
      s_cloudDialMu = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_cloudDialMux);
  }
  return s_cloudDialMu;
}

// ============================================================================
// CloudTunnel implementation
// ============================================================================

CloudTunnel::~CloudTunnel() { stop_(); }

void CloudTunnel::configure(const char *baseUrl, const char *deviceId,
                            const char *token, const char *tunnelKey) {
  char nextBase[sizeof(baseUrl_)] = {0};
  char nextDev[sizeof(deviceId_)] = {0};
  char nextTok[sizeof(token_)] = {0};
  char nextKey[sizeof(tunnelKey_)] = {0};

  safeCopy(nextBase, sizeof(nextBase), baseUrl ? baseUrl : "");
  safeCopy(nextDev, sizeof(nextDev), deviceId ? deviceId : "");
  safeCopy(nextTok, sizeof(nextTok), token ? token : "");
  safeCopy(nextKey, sizeof(nextKey), tunnelKey ? tunnelKey : "");

  const bool unchanged =
      (strcmp(nextBase, baseUrl_) == 0) && (strcmp(nextDev, deviceId_) == 0) &&
      (strcmp(nextTok, token_) == 0) && (strcmp(nextKey, tunnelKey_) == 0);

  safeCopy(baseUrl_, sizeof(baseUrl_), nextBase);
  safeCopy(deviceId_, sizeof(deviceId_), nextDev);
  safeCopy(token_, sizeof(token_), nextTok);
  safeCopy(tunnelKey_, sizeof(tunnelKey_), nextKey);

  if (enabled_ && !unchanged) {
    connected_ = false;
    SemaphoreHandle_t mu = (SemaphoreHandle_t)mutex_;
    const bool locked =
        (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(200)) == pdTRUE);
    esp_websocket_client_handle_t c = (esp_websocket_client_handle_t)client_;
    client_ = nullptr;
    if (c) {
      (void)esp_websocket_client_stop(c);
      esp_websocket_client_destroy(c);
    }
    if (locked) {
      (void)xSemaphoreGive(mu);
    }
    startTask_();
  }
}

void CloudTunnel::setEnabled(bool enabled) {
  if (!enabled) {
    enabled_ = false;
    stop_();
    return;
  }
  enabled_ = true;
  startTask_();
}

void CloudTunnel::setCallbacks(OnMessageCb onMessage, OnConnectCb onConnect,
                               OnDisconnectCb onDisconnect, void *userCtx) {
  onMessage_ = onMessage;
  onConnect_ = onConnect;
  onDisconnect_ = onDisconnect;
  userCtx_ = userCtx;
}

esp_err_t CloudTunnel::sendText(const char *message, size_t len) {
  if (!connected_ || !enabled_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (message == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }

  SemaphoreHandle_t mu = (SemaphoreHandle_t)mutex_;
  const bool locked =
      (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(25)) == pdTRUE);
  if (mu != nullptr && !locked) {
    return ESP_ERR_TIMEOUT;
  }
  esp_websocket_client_handle_t c = (esp_websocket_client_handle_t)client_;
  const bool conn = (c != nullptr) && esp_websocket_client_is_connected(c);
  int sent = -1;
  if (conn) {
    sent = esp_websocket_client_send_text(c, message, (int)len, 200);
  }
  if (locked) {
    (void)xSemaphoreGive(mu);
  }
  if (sent > 0 || len == 0) {
    return ESP_OK;
  }
  if (sent == 0) {
    return ESP_ERR_TIMEOUT;
  }
  return ESP_FAIL;
}

esp_err_t CloudTunnel::sendBinary(const uint8_t *data, size_t len) {
  if (!connected_ || !enabled_) {
    return ESP_ERR_INVALID_STATE;
  }
  if (data == nullptr && len > 0) {
    return ESP_ERR_INVALID_ARG;
  }

  SemaphoreHandle_t mu = (SemaphoreHandle_t)mutex_;
  const bool locked =
      (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(5)) == pdTRUE);
  if (mu != nullptr && !locked) {
    // Don't block - drop frame if mutex busy
    return ESP_ERR_TIMEOUT;
  }

  esp_websocket_client_handle_t c = (esp_websocket_client_handle_t)client_;
  const bool conn = (c != nullptr) && esp_websocket_client_is_connected(c);
  int sent = -1;
  if (conn) {
    // Use VERY SHORT timeout - aggressively drop frames over blocking
    // Camera sends frames every 100-300ms (3-10 FPS)
    // Cloud tunnel has higher latency, so be very aggressive
    const int timeoutMs = 10; // Reduced to 10ms - fail fast
    sent = esp_websocket_client_send_bin(c, (const char *)data, (int)len,
                                         timeoutMs);
  }
  if (locked) {
    (void)xSemaphoreGive(mu);
  }

  if (sent > 0 || len == 0) {
    return ESP_OK;
  }
  if (sent == 0) {
    return ESP_ERR_TIMEOUT;
  }
  return ESP_FAIL;
}

void CloudTunnel::eventHandler_(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
  (void)base;
  CloudTunnel *ct = static_cast<CloudTunnel *>(handler_args);
  if (ct == nullptr) {
    return;
  }

  if (event_id == WEBSOCKET_EVENT_CONNECTED) {
    ct->connected_ = true;
    ct->uiConnected_ = false;
    if (ct->onConnect_) {
      ct->onConnect_(ct->userCtx_);
    }
    return;
  }

  if (event_id == WEBSOCKET_EVENT_DISCONNECTED) {
    ct->connected_ = false;
    ct->uiConnected_ = false;
    if (ct->onDisconnect_) {
      ct->onDisconnect_(ct->userCtx_);
    }
    return;
  }

  if (event_id == WEBSOCKET_EVENT_DATA) {
    esp_websocket_event_data_t *d = (esp_websocket_event_data_t *)event_data;
    if (d == nullptr) {
      return;
    }
    if (d->payload_offset != 0 || d->payload_len != d->data_len) {
      return;
    }

    httpd_ws_type_t t = HTTPD_WS_TYPE_BINARY;
    if (d->op_code == 0x1) {
      t = HTTPD_WS_TYPE_TEXT;
    } else if (d->op_code == 0x2) {
      t = HTTPD_WS_TYPE_BINARY;
    } else if (d->op_code == 0x8) {
      t = HTTPD_WS_TYPE_CLOSE;
    } else {
      return;
    }

    // Intercept broker control messages
    if (t == HTTPD_WS_TYPE_TEXT && d->data_ptr != nullptr && d->data_len > 0) {
      char typeC[32] = {0};
      if (jsonGetStringField((const char *)d->data_ptr, (size_t)d->data_len,
                             "type", typeC, sizeof(typeC))) {
        bool handledBrokerMeta = false;
        if (strcmp(typeC, "registered") == 0) {
          char ui[sizeof(ct->uiWSURL_)] = {0};
          char dev[sizeof(ct->deviceWSURL_)] = {0};
          (void)jsonGetStringField((const char *)d->data_ptr,
                                   (size_t)d->data_len, "ui_ws_url", ui,
                                   sizeof(ui));
          (void)jsonGetStringField((const char *)d->data_ptr,
                                   (size_t)d->data_len, "device_ws_url", dev,
                                   sizeof(dev));
          if (ui[0]) {
            safeCopy(ct->uiWSURL_, sizeof(ct->uiWSURL_), ui);
          }
          if (dev[0]) {
            safeCopy(ct->deviceWSURL_, sizeof(ct->deviceWSURL_), dev);
          }
          ct->registeredAtMs_ = (uint32_t)(esp_timer_get_time() / 1000ULL);
          handledBrokerMeta = true;
        } else if (strcmp(typeC, "ui_connected") == 0) {
          ct->uiConnected_ = true;
          handledBrokerMeta = true;
        } else if (strcmp(typeC, "ui_disconnected") == 0) {
          ct->uiConnected_ = false;
          handledBrokerMeta = true;
        }

        if (handledBrokerMeta) {
          return;
        }
      }
    }

    if (ct->onMessage_) {
      ct->onMessage_(t, (const uint8_t *)d->data_ptr, (size_t)d->data_len,
                     ct->userCtx_);
    }
    return;
  }
}

void CloudTunnel::startTask_() {
  if (task_ != nullptr) {
    return;
  }
  if (mutex_ == nullptr) {
    mutex_ = (void *)xSemaphoreCreateMutex();
  }
  TaskHandle_t th = nullptr;
  const BaseType_t ok = xTaskCreate(&CloudTunnel::taskTrampoline_, "cloudTnl",
                                    8192, this, 5, &th);
  if (ok == pdPASS) {
    task_ = (void *)th;
  }
}

void CloudTunnel::stop_() {
  connected_ = false;

  SemaphoreHandle_t mu = (SemaphoreHandle_t)mutex_;
  const bool locked =
      (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(200)) == pdTRUE);

  esp_websocket_client_handle_t c = (esp_websocket_client_handle_t)client_;
  client_ = nullptr;
  if (c) {
    (void)esp_websocket_client_stop(c);
    esp_websocket_client_destroy(c);
  }

  if (locked) {
    (void)xSemaphoreGive(mu);
  }
}

void CloudTunnel::taskTrampoline_(void *arg) {
  CloudTunnel *ct = static_cast<CloudTunnel *>(arg);
  if (ct == nullptr) {
    vTaskDelete(nullptr);
    return;
  }

  int backoffMs = 1000;
  for (;;) {
    if (!ct->enabled_) {
      break;
    }
    if (ct->baseUrl_[0] == '\0' || ct->deviceId_[0] == '\0') {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    // Build URL:
    // <base>/ws/device/<deviceId>?announce=1&tunnel=<key>&token=<token>
    char encTunnel[CloudTunnel::kMaxTunnelKeyLen * 3] = {0};
    char encToken[CloudTunnel::kMaxTokenLen * 3] = {0};
    urlEncode(ct->tunnelKey_, encTunnel, sizeof(encTunnel));
    urlEncode(ct->token_, encToken, sizeof(encToken));

    char url[768] = {0};
    const bool hasTunnel = (encTunnel[0] != '\0');
    const bool hasToken = (encToken[0] != '\0');
    safeAppend(url, sizeof(url), ct->baseUrl_);
    safeAppend(url, sizeof(url), "/ws/device/");
    safeAppend(url, sizeof(url), ct->deviceId_);
    safeAppend(url, sizeof(url), "?announce=1");
    if (hasTunnel) {
      safeAppend(url, sizeof(url), "&tunnel=");
      safeAppend(url, sizeof(url), encTunnel);
    }
    if (hasToken) {
      safeAppend(url, sizeof(url), "&token=");
      safeAppend(url, sizeof(url), encToken);
    }
    if (url[sizeof(url) - 1] != '\0') {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    esp_websocket_client_config_t cfg = {};
    cfg.uri = url;
    cfg.buffer_size = 8192;
    cfg.task_stack = 6144;
    cfg.task_prio = 5;
    cfg.network_timeout_ms = 15000;
    cfg.disable_auto_reconnect = true;

#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    SemaphoreHandle_t dial = cloudDialMu();
    const bool dialLocked =
        (dial != nullptr) &&
        (xSemaphoreTake(dial, pdMS_TO_TICKS(15000)) == pdTRUE);

    SemaphoreHandle_t mu = (SemaphoreHandle_t)ct->mutex_;
    const bool locked =
        (mu != nullptr) && (xSemaphoreTake(mu, pdMS_TO_TICKS(200)) == pdTRUE);
    esp_websocket_client_handle_t c = esp_websocket_client_init(&cfg);
    if (c) {
      ct->client_ = (void *)c;
      (void)esp_websocket_register_events(c, WEBSOCKET_EVENT_ANY,
                                          &CloudTunnel::eventHandler_, ct);
      (void)esp_websocket_client_start(c);
    }
    if (locked) {
      (void)xSemaphoreGive(mu);
    }

    if (dialLocked) {
      const int64_t holdStartUs = esp_timer_get_time();
      while (ct->enabled_ && !ct->connected_) {
        const int64_t heldMs = (esp_timer_get_time() - holdStartUs) / 1000;
        if (heldMs > 2000) {
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      (void)xSemaphoreGive(dial);
    }

    const int64_t connectStartUs = esp_timer_get_time();
    while (ct->enabled_ && !ct->connected_) {
      const int64_t waitedMs = (esp_timer_get_time() - connectStartUs) / 1000;
      if (waitedMs > 15000) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    const int64_t startUs = esp_timer_get_time();
    while (ct->enabled_) {
      if (!ct->connected_) {
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(250));
    }

    ct->connected_ = false;
    SemaphoreHandle_t mu3 = (SemaphoreHandle_t)ct->mutex_;
    const bool locked3 =
        (mu3 != nullptr) && (xSemaphoreTake(mu3, pdMS_TO_TICKS(200)) == pdTRUE);
    esp_websocket_client_handle_t c3 =
        (esp_websocket_client_handle_t)ct->client_;
    ct->client_ = nullptr;
    if (c3) {
      (void)esp_websocket_client_stop(c3);
      esp_websocket_client_destroy(c3);
    }
    if (locked3) {
      (void)xSemaphoreGive(mu3);
    }

    const int64_t durMs = (esp_timer_get_time() - startUs) / 1000;
    if (durMs > 30000) {
      backoffMs = 1000;
    } else if (backoffMs < 30000) {
      backoffMs *= 2;
      if (backoffMs > 30000) {
        backoffMs = 30000;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(backoffMs));
  }

  ct->task_ = nullptr;
  vTaskDelete(nullptr);
}
