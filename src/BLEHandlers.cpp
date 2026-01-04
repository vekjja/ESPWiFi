/**
 * @file BLEHandlers.cpp
 * @brief BLE event handlers and callbacks for ESPWiFi
 *
 * This file implements BLE (Bluetooth Low Energy) event handlers and NimBLE
 * stack callbacks. BLE events include GAP connection/disconnection, advertising
 * lifecycle, host synchronization, and reset events. Follows ESP-IDF component
 * architecture best practices with static callback wrappers and instance
 * methods.
 */

#include "ESPWiFi.h"
#ifdef CONFIG_BT_NIMBLE_ENABLED

#ifndef ESPWiFi_BLE_HANDLERS
#define ESPWiFi_BLE_HANDLERS

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

// ============================================================================
// Constants and TAG for ESP-IDF logging
// ============================================================================

static const char *BLE_HANDLER_TAG = "ESPWiFi_BLE_Handler";

// Cast helper macro with null check
#define ESPWiFi_OBJ_CAST(obj)                                                  \
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(obj);                              \
  if (!espwifi) {                                                              \
    ESP_LOGE(BLE_HANDLER_TAG, "Invalid ESPWiFi instance pointer");             \
    return;                                                                    \
  }

// ============================================================================
// BLE Event Handler Callbacks (Instance Methods)
// ============================================================================

/**
 * @brief BLE GAP connection event handler
 *
 * Called when a BLE device connects or connection attempt completes.
 * Logs the connection status and handles connection failures.
 *
 * @param status Connection status (0 = success)
 * @param conn_handle Connection handle for the new connection
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleConnectionHandler(int status, uint16_t conn_handle,
                                   void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  if (status == 0) {
    espwifi->log(INFO, "🔵 BLE Connection established (handle=%d)",
                 conn_handle);
  } else {
    espwifi->log(WARNING, "🔵 BLE Connection failed, status=%d", status);
  }
}

/**
 * @brief BLE GAP disconnection event handler
 *
 * Called when a BLE device disconnects. Logs the disconnection reason.
 *
 * @param reason Disconnect reason code
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleDisconnectionHandler(int reason, void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  espwifi->log(INFO, "🔵 BLE Disconnected, reason=%d", reason);
}

/**
 * @brief BLE advertising complete event handler
 *
 * Called when advertising cycle completes or is stopped.
 *
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleAdvertisingCompleteHandler(void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  espwifi->log(DEBUG, "🔵 BLE Advertising complete");
}

/**
 * @brief BLE subscription event handler
 *
 * Called when a client subscribes to notifications/indications.
 *
 * @param conn_handle Connection handle of subscribing client
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleSubscribeHandler(uint16_t conn_handle, void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  espwifi->log(INFO, "🔵 BLE Subscribe event, conn_handle=%d", conn_handle);
}

/**
 * @brief BLE MTU update event handler
 *
 * Called when the MTU (Maximum Transmission Unit) is negotiated.
 *
 * @param conn_handle Connection handle
 * @param mtu Negotiated MTU size
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleMtuUpdateHandler(uint16_t conn_handle, uint16_t mtu,
                                  void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  espwifi->log(INFO, "🔵 BLE MTU update, conn_handle=%d mtu=%d", conn_handle,
               mtu);
}

/**
 * @brief BLE host synchronization handler
 *
 * Called when the NimBLE host and controller become synced.
 * Starts advertising and logs the BLE address.
 *
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleHostSyncHandler(void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  espwifi->log(INFO, "🔵 BLE Host and Controller synced");

  // Get and log the BLE address
  std::string address = espwifi->getBLEAddress();
  if (!address.empty()) {
    espwifi->log(INFO, "🔵 BLE Address: %s", address.c_str());
  }
  // Start advertising
  espwifi->startBLEAdvertising();
}

/**
 * @brief BLE host reset handler
 *
 * Called when the NimBLE host resets (typically due to fatal error).
 *
 * @param reason Reset reason code
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleHostResetHandler(int reason, void *obj) {
  ESPWiFi_OBJ_CAST(obj);
  espwifi->log(WARNING, "🔵 BLE Host reset, reason=%d", reason);
}

/**
 * @brief BLE host task started handler
 *
 * Called when the NimBLE host task begins execution.
 *
 * @param obj Pointer to ESPWiFi instance
 */
void ESPWiFi::bleHostTaskStartedHandler(void *obj) {
  ESPWiFi_OBJ_CAST(obj);

  espwifi->log(INFO, "🔵 BLE Host Task Started");
}

// ============================================================================
// Static Callback Wrappers for NimBLE Stack
// ============================================================================

/**
 * @brief Static wrapper for BLE GAP event callback
 *
 * Receives GAP events from NimBLE stack and routes them to instance methods.
 * The 'arg' parameter should be a pointer to the ESPWiFi instance.
 */
int ESPWiFi::bleGapEventCallbackStatic(struct ble_gap_event *event, void *arg) {
  if (!arg) {
    return 0;
  }

  ESPWiFi *espwifi = static_cast<ESPWiFi *>(arg);

  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    espwifi->bleConnectionHandler(event->connect.status,
                                  event->connect.conn_handle, arg);
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    espwifi->bleDisconnectionHandler(event->disconnect.reason, arg);
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    espwifi->bleAdvertisingCompleteHandler(arg);
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    espwifi->bleSubscribeHandler(event->subscribe.conn_handle, arg);
    break;

  case BLE_GAP_EVENT_MTU:
    espwifi->bleMtuUpdateHandler(event->mtu.conn_handle, event->mtu.value, arg);
    break;

  default:
    espwifi->log(DEBUG, "🔵 BLE GAP event: %d", event->type);
    break;
  }

  return 0;
}

/**
 * @brief Static wrapper for NimBLE host sync callback
 *
 * Called when host and controller sync. Stored in ble_hs_cfg.sync_cb.
 */
void ESPWiFi::bleHostSyncCallbackStatic(void *arg) {
  if (!arg) {
    return;
  }
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(arg);
  espwifi->bleHostSyncHandler(arg);
}

/**
 * @brief Static wrapper for NimBLE host reset callback
 *
 * Called when host resets. Stored in ble_hs_cfg.reset_cb.
 */
void ESPWiFi::bleHostResetCallbackStatic(int reason, void *arg) {
  if (!arg) {
    return;
  }
  ESPWiFi *espwifi = static_cast<ESPWiFi *>(arg);
  espwifi->bleHostResetHandler(reason, arg);
}

/**
 * @brief Static wrapper for BLE host task
 *
 * FreeRTOS task that runs the NimBLE host stack event loop.
 */
void ESPWiFi::bleHostTaskStatic(void *arg) {
  if (arg) {
    ESPWiFi *espwifi = static_cast<ESPWiFi *>(arg);
    espwifi->bleHostTaskStartedHandler(arg);
  }

  // This function will return only when nimble_port_stop() is executed
  nimble_port_run();

  // Clean up after host task ends
  nimble_port_freertos_deinit();
}

#endif // ESPWiFi_BLE_HANDLERS
#endif // CONFIG_BT_NIMBLE_ENABLED
