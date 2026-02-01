#include "WebSocketClient.h"

#include <cstring>
#include <memory>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WebSocketClient";

//==============================================================================
// Constructor / Destructor
//==============================================================================

WebSocketClient::WebSocketClient() {
  recvBuffer_ = new (std::nothrow) uint8_t[kMaxMessageLen];
  recvBufferSize_ = recvBuffer_ ? kMaxMessageLen : 0;
}

WebSocketClient::~WebSocketClient() {
  close();
  if (recvBuffer_) {
    delete[] recvBuffer_;
    recvBuffer_ = nullptr;
  }
}

//==============================================================================
// Event Handler
//==============================================================================

void WebSocketClient::eventHandlerTrampoline(void *handlerArgs,
                                             esp_event_base_t base,
                                             int32_t eventId, void *eventData) {
  if (handlerArgs == nullptr) {
    return;
  }

  WebSocketClient *instance = static_cast<WebSocketClient *>(handlerArgs);
  instance->handleEvent(base, eventId, eventData);
}

void WebSocketClient::handleEvent(esp_event_base_t base, int32_t eventId,
                                  void *eventData) {
  esp_websocket_event_data_t *data =
      static_cast<esp_websocket_event_data_t *>(eventData);

  switch (eventId) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "WebSocket connected");
    connected_ = true;
    reconnectAttempts_ = 0;

    if (onConnect_) {
      onConnect_();
    }
    break;

  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "WebSocket disconnected");
    connected_ = false;

    if (onDisconnect_) {
      onDisconnect_();
    }

    // Handle reconnection
    if (autoReconnect_) {
      scheduleReconnect();
    }
    break;

  case WEBSOCKET_EVENT_DATA:
    if (data && onMessage_) {
      bool isBinary = (data->op_code == 0x02); // Binary frame

      // Handle fragmented messages
      if (data->data_len > 0 && data->data_ptr) {
        onMessage_(reinterpret_cast<const uint8_t *>(data->data_ptr),
                   data->data_len, isBinary);
      }
    }
    break;

  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGE(TAG, "WebSocket error");

    if (onError_) {
      onError_(ESP_FAIL);
    }
    break;

    // Note: WEBSOCKET_EVENT_PING and WEBSOCKET_EVENT_PONG are not available
    // in esp_websocket_client 1.5.0. Ping/pong is handled automatically.

  default:
    break;
  }
}

//==============================================================================
// Initialization
//==============================================================================

bool WebSocketClient::begin(const char *uri) {
  Config config;
  config.uri = uri;
  return begin(config);
}

bool WebSocketClient::begin(const Config &config) {
  if (config.uri == nullptr) {
    ESP_LOGE(TAG, "URI cannot be null");
    return false;
  }

  // Store URI
  snprintf(uri_, sizeof(uri_), "%s", config.uri);

  // Store reconnection settings
  autoReconnect_ = config.autoReconnect;
  reconnectDelay_ = config.reconnectDelay;
  maxReconnectAttempts_ = config.maxReconnectAttempts;

  // Store configuration for reconnection
  bufferSize_ = config.bufferSize;
  pingInterval_ = config.pingInterval;
  timeout_ = config.timeout;
  disableCertVerify_ = config.disableCertVerify;
  certPem_ = config.certPem;

  // Configure WebSocket client
  esp_websocket_client_config_t wsConfig = {};
  wsConfig.uri = config.uri;

  if (config.subprotocol) {
    wsConfig.subprotocol = config.subprotocol;
  }

  if (config.userAgent) {
    wsConfig.user_agent = config.userAgent;
  }

  // Authentication
  if (config.authUser && config.authPass) {
    wsConfig.username = config.authUser;
    wsConfig.password = config.authPass;
  } else if (config.authToken) {
    // Set Bearer token as header
    snprintf(authHeader_, sizeof(authHeader_), "Authorization: Bearer %s",
             config.authToken);
    wsConfig.headers = authHeader_;
  }

  // TLS configuration for wss:// connections
  if (strncmp(config.uri, "wss://", 6) == 0) {
    wsConfig.transport = WEBSOCKET_TRANSPORT_OVER_SSL;

    if (config.certPem) {
      // Use provided certificate
      wsConfig.cert_pem = config.certPem;
    } else {
      // Use ESP-IDF's built-in certificate bundle for verification
      // This includes common CA certificates for HTTPS
      wsConfig.crt_bundle_attach = esp_crt_bundle_attach;
      wsConfig.skip_cert_common_name_check = false;
    }

    // Allow override to disable certificate verification
    if (config.disableCertVerify) {
      wsConfig.skip_cert_common_name_check = true;
      wsConfig.crt_bundle_attach = NULL;
    }
  } else {
    // Non-TLS connections
    if (config.certPem) {
      wsConfig.cert_pem = config.certPem;
    }
    if (config.disableCertVerify) {
      wsConfig.skip_cert_common_name_check = true;
    }
  }

  // Buffer size
  wsConfig.buffer_size = config.bufferSize;

  // Task stack size - reduce to fit alongside BLE during startup
  wsConfig.task_stack = 6144;

  // Ping/Pong settings
  if (config.pingInterval > 0) {
    wsConfig.ping_interval_sec = config.pingInterval / 1000;
  }

  // Network timeout
  wsConfig.network_timeout_ms = config.timeout;

  // Disable auto-reconnect (we handle it ourselves for more control)
  wsConfig.reconnect_timeout_ms = 0;
  wsConfig.disable_auto_reconnect = true;

  // Create client
  client_ = esp_websocket_client_init(&wsConfig);
  if (client_ == nullptr) {
    ESP_LOGE(TAG, "Failed to initialize WebSocket client");
    return false;
  }

  // Register event handler
  esp_err_t err = esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY,
                                                eventHandlerTrampoline, this);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(err));
    esp_websocket_client_destroy(client_);
    client_ = nullptr;
    return false;
  }

  // Start the client
  return connect();
}

//==============================================================================
// Connection Management
//==============================================================================

bool WebSocketClient::connect() {
  if (client_ == nullptr) {
    ESP_LOGE(TAG, "Client not initialized");
    return false;
  }

  if (connected_) {
    ESP_LOGW(TAG, "Already connected");
    return true;
  }

  ESP_LOGI(TAG, "Connecting to %s", uri_);

  esp_err_t err = esp_websocket_client_start(client_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
    // Clean up failed connection attempt to prevent memory leaks
    // Stop and destroy the client to free all resources
    // This is safe because we're not in the client's task context
    esp_websocket_client_stop(client_);
    esp_websocket_client_destroy(client_);
    client_ = nullptr;
    return false;
  }

  return true;
}

void WebSocketClient::disconnect() {
  autoReconnect_ = false; // Disable auto-reconnect
  close();
}

bool WebSocketClient::reconnect() {
  disconnect();
  vTaskDelay(pdMS_TO_TICKS(100)); // Small delay
  return connect();
}

void WebSocketClient::scheduleReconnect() {
  // Check if we've exceeded max attempts
  if (maxReconnectAttempts_ > 0 &&
      reconnectAttempts_ >= maxReconnectAttempts_) {
    ESP_LOGW(TAG, "Max reconnect attempts (%lu) reached",
             maxReconnectAttempts_);
    // Disable auto-reconnect to prevent further attempts
    autoReconnect_ = false;
    return;
  }

  reconnectAttempts_++;

  // Exponential backoff: delay increases with each attempt (capped at 60s)
  uint32_t delay = reconnectDelay_;
  if (reconnectAttempts_ > 1) {
    delay = reconnectDelay_ * (1 << (reconnectAttempts_ - 1));
    if (delay > 60000) {
      delay = 60000; // Cap at 60 seconds
    }
  }

  ESP_LOGI(TAG, "Scheduling reconnect attempt %lu in %lu ms",
           reconnectAttempts_, delay);

  // Clean up the failed client before reconnecting
  // This prevents memory leaks from accumulated failed connection attempts
  // Note: We're in the event handler task, so we need to be careful
  // Just stop it - if connect() fails again, we'll handle cleanup there
  if (client_ != nullptr && !connected_) {
    esp_websocket_client_stop(client_);
    // Don't destroy here - we're in the client's task context
    // The next connect() will handle it, or we'll destroy on next failure
  }

  // Schedule reconnection in a task
  // Note: In production, you might want to use a timer or event group
  vTaskDelay(pdMS_TO_TICKS(delay));

  // Reinitialize the client if it was destroyed
  if (client_ == nullptr) {
    // Recreate the client configuration
    esp_websocket_client_config_t wsConfig = {};
    wsConfig.uri = uri_;

    // Reapply TLS configuration if needed
    if (strncmp(uri_, "wss://", 6) == 0) {
      wsConfig.transport = WEBSOCKET_TRANSPORT_OVER_SSL;

      if (certPem_) {
        wsConfig.cert_pem = certPem_;
      } else {
        wsConfig.crt_bundle_attach = esp_crt_bundle_attach;
        wsConfig.skip_cert_common_name_check = false;
      }

      if (disableCertVerify_) {
        wsConfig.skip_cert_common_name_check = true;
        wsConfig.crt_bundle_attach = NULL;
      }
    }

    wsConfig.buffer_size = bufferSize_;
    wsConfig.task_stack = 6144;
    wsConfig.network_timeout_ms = timeout_;
    wsConfig.reconnect_timeout_ms = 0;
    wsConfig.disable_auto_reconnect = true;

    if (pingInterval_ > 0) {
      wsConfig.ping_interval_sec = pingInterval_ / 1000;
    }

    if (authHeader_[0] != '\0') {
      wsConfig.headers = authHeader_;
    }

    client_ = esp_websocket_client_init(&wsConfig);
    if (client_ == nullptr) {
      ESP_LOGE(TAG, "Failed to reinitialize WebSocket client (out of memory?)");
      // Give up on this reconnect attempt - wait longer before next try
      reconnectAttempts_--; // Don't count this as an attempt since we couldn't
                            // even init
      return;
    }

    // Re-register event handler
    esp_err_t err = esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY,
                                                  eventHandlerTrampoline, this);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register event handler: %s",
               esp_err_to_name(err));
      esp_websocket_client_destroy(client_);
      client_ = nullptr;
      reconnectAttempts_--; // Don't count this as an attempt
      return;
    }
  }

  connect();
}

void WebSocketClient::close() {
  if (client_ == nullptr) {
    return;
  }

  connected_ = false;

  esp_err_t err = esp_websocket_client_close(client_, portMAX_DELAY);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Error closing WebSocket: %s", esp_err_to_name(err));
  }

  err = esp_websocket_client_stop(client_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Error stopping WebSocket: %s", esp_err_to_name(err));
  }

  esp_websocket_client_destroy(client_);
  client_ = nullptr;
}

//==============================================================================
// Send Operations
//==============================================================================

esp_err_t WebSocketClient::sendText(const char *message) {
  if (message == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  return sendText(message, strlen(message));
}

esp_err_t WebSocketClient::sendText(const char *message, size_t len) {
  if (!connected_ || client_ == nullptr) {
    ESP_LOGE(TAG, "Not connected");
    return ESP_ERR_INVALID_STATE;
  }

  if (message == nullptr || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  int sent =
      esp_websocket_client_send_text(client_, message, len, portMAX_DELAY);
  if (sent < 0) {
    ESP_LOGE(TAG, "Failed to send text message");
    return ESP_FAIL;
  }

  ESP_LOGD(TAG, "Sent %d bytes (text)", sent);
  return ESP_OK;
}

esp_err_t WebSocketClient::sendBinary(const uint8_t *data, size_t len) {
  if (!connected_ || client_ == nullptr) {
    ESP_LOGE(TAG, "Not connected");
    return ESP_ERR_INVALID_STATE;
  }

  if (data == nullptr || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  int sent = esp_websocket_client_send_bin(
      client_, reinterpret_cast<const char *>(data), len, portMAX_DELAY);

  if (sent < 0) {
    ESP_LOGE(TAG, "Failed to send binary message");
    return ESP_FAIL;
  }

  ESP_LOGD(TAG, "Sent %d bytes (binary)", sent);
  return ESP_OK;
}

esp_err_t WebSocketClient::sendPing() {
  if (!connected_ || client_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  // Send empty ping frame
  int sent = esp_websocket_client_send_text(client_, "", 0, portMAX_DELAY);
  return (sent >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t WebSocketClient::sendPong() {
  if (!connected_ || client_ == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  // Note: ESP-IDF WebSocket client handles pong automatically
  // This is here for manual control if needed
  return ESP_OK;
}
