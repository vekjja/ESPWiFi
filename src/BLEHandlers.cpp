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

// ============================================================================
// Small helpers for readable logs (event numbers / reason codes)
// ============================================================================

static const char *ble_gap_event_type_to_str(int type) {
  switch (type) {
  case BLE_GAP_EVENT_CONNECT:
    return "BLE_GAP_EVENT_CONNECT";
  case BLE_GAP_EVENT_DISCONNECT:
    return "BLE_GAP_EVENT_DISCONNECT";
  case BLE_GAP_EVENT_CONN_UPDATE:
    return "BLE_GAP_EVENT_CONN_UPDATE";
  case BLE_GAP_EVENT_CONN_UPDATE_REQ:
    return "BLE_GAP_EVENT_CONN_UPDATE_REQ";
  case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
    return "BLE_GAP_EVENT_L2CAP_UPDATE_REQ";
  case BLE_GAP_EVENT_TERM_FAILURE:
    return "BLE_GAP_EVENT_TERM_FAILURE";
  case BLE_GAP_EVENT_ADV_COMPLETE:
    return "BLE_GAP_EVENT_ADV_COMPLETE";
  case BLE_GAP_EVENT_ENC_CHANGE:
    return "BLE_GAP_EVENT_ENC_CHANGE";
  case BLE_GAP_EVENT_SUBSCRIBE:
    return "BLE_GAP_EVENT_SUBSCRIBE";
  case BLE_GAP_EVENT_MTU:
    return "BLE_GAP_EVENT_MTU";
  case BLE_GAP_EVENT_NOTIFY_RX:
    return "BLE_GAP_EVENT_NOTIFY_RX";
  case BLE_GAP_EVENT_NOTIFY_TX:
    return "BLE_GAP_EVENT_NOTIFY_TX";
  case BLE_GAP_EVENT_REPEAT_PAIRING:
    return "BLE_GAP_EVENT_REPEAT_PAIRING";
  case BLE_GAP_EVENT_PASSKEY_ACTION:
    return "BLE_GAP_EVENT_PASSKEY_ACTION";
  case BLE_GAP_EVENT_IDENTITY_RESOLVED:
    return "BLE_GAP_EVENT_IDENTITY_RESOLVED";
  case BLE_GAP_EVENT_DISC:
    return "BLE_GAP_EVENT_DISC";
  case BLE_GAP_EVENT_DISC_COMPLETE:
    return "BLE_GAP_EVENT_DISC_COMPLETE";
#ifdef BLE_GAP_EVENT_PHY_UPDATE_COMPLETE
  case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
    return "BLE_GAP_EVENT_PHY_UPDATE_COMPLETE";
#endif
#ifdef BLE_GAP_EVENT_EXT_DISC
  case BLE_GAP_EVENT_EXT_DISC:
    return "BLE_GAP_EVENT_EXT_DISC";
#endif
#ifdef BLE_GAP_EVENT_PERIODIC_SYNC
  case BLE_GAP_EVENT_PERIODIC_SYNC:
    return "BLE_GAP_EVENT_PERIODIC_SYNC";
#endif
#ifdef BLE_GAP_EVENT_PERIODIC_REPORT
  case BLE_GAP_EVENT_PERIODIC_REPORT:
    return "BLE_GAP_EVENT_PERIODIC_REPORT";
#endif
#ifdef BLE_GAP_EVENT_PERIODIC_SYNC_LOST
  case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
    return "BLE_GAP_EVENT_PERIODIC_SYNC_LOST";
#endif
#ifdef BLE_GAP_EVENT_SCAN_REQ_RCVD
  case BLE_GAP_EVENT_SCAN_REQ_RCVD:
    return "BLE_GAP_EVENT_SCAN_REQ_RCVD";
#endif
#ifdef BLE_GAP_EVENT_PERIODIC_TRANSFER
  case BLE_GAP_EVENT_PERIODIC_TRANSFER:
    return "BLE_GAP_EVENT_PERIODIC_TRANSFER";
#endif
#ifdef BLE_GAP_EVENT_PATHLOSS_THRESHOLD
  case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
    return "BLE_GAP_EVENT_PATHLOSS_THRESHOLD";
#endif
#ifdef BLE_GAP_EVENT_TRANSMIT_POWER
  case BLE_GAP_EVENT_TRANSMIT_POWER:
    return "BLE_GAP_EVENT_TRANSMIT_POWER";
#endif
#ifdef BLE_GAP_EVENT_PARING_COMPLETE
  case BLE_GAP_EVENT_PARING_COMPLETE:
    return "BLE_GAP_EVENT_PARING_COMPLETE";
#endif
#ifdef BLE_GAP_EVENT_SUBRATE_CHANGE
  case BLE_GAP_EVENT_SUBRATE_CHANGE:
    return "BLE_GAP_EVENT_SUBRATE_CHANGE";
#endif
#ifdef BLE_GAP_EVENT_VS_HCI
  case BLE_GAP_EVENT_VS_HCI:
    return "BLE_GAP_EVENT_VS_HCI";
#endif
#ifdef BLE_GAP_EVENT_BIGINFO_REPORT
  case BLE_GAP_EVENT_BIGINFO_REPORT:
    return "BLE_GAP_EVENT_BIGINFO_REPORT";
#endif
#ifdef BLE_GAP_EVENT_REATTEMPT_COUNT
  case BLE_GAP_EVENT_REATTEMPT_COUNT:
    return "BLE_GAP_EVENT_REATTEMPT_COUNT";
#endif
#ifdef BLE_GAP_EVENT_AUTHORIZE
  case BLE_GAP_EVENT_AUTHORIZE:
    return "BLE_GAP_EVENT_AUTHORIZE";
#endif
#ifdef BLE_GAP_EVENT_TEST_UPDATE
  case BLE_GAP_EVENT_TEST_UPDATE:
    return "BLE_GAP_EVENT_TEST_UPDATE";
#endif
#ifdef BLE_GAP_EVENT_DATA_LEN_CHG
  case BLE_GAP_EVENT_DATA_LEN_CHG:
    return "BLE_GAP_EVENT_DATA_LEN_CHG";
#endif
#ifdef BLE_GAP_EVENT_CONNLESS_IQ_REPORT
  case BLE_GAP_EVENT_CONNLESS_IQ_REPORT:
    return "BLE_GAP_EVENT_CONNLESS_IQ_REPORT";
#endif
#ifdef BLE_GAP_EVENT_CONN_IQ_REPORT
  case BLE_GAP_EVENT_CONN_IQ_REPORT:
    return "BLE_GAP_EVENT_CONN_IQ_REPORT";
#endif
#ifdef BLE_GAP_EVENT_CTE_REQ_FAILED
  case BLE_GAP_EVENT_CTE_REQ_FAILED:
    return "BLE_GAP_EVENT_CTE_REQ_FAILED";
#endif
#ifdef BLE_GAP_EVENT_LINK_ESTAB
  case BLE_GAP_EVENT_LINK_ESTAB:
    return "BLE_GAP_EVENT_LINK_ESTAB";
#endif
#ifdef BLE_GAP_EVENT_EATT
  case BLE_GAP_EVENT_EATT:
    return "BLE_GAP_EVENT_EATT";
#endif
#ifdef BLE_GAP_EVENT_PER_SUBEV_DATA_REQ
  case BLE_GAP_EVENT_PER_SUBEV_DATA_REQ:
    return "BLE_GAP_EVENT_PER_SUBEV_DATA_REQ";
#endif
#ifdef BLE_GAP_EVENT_PER_SUBEV_RESP
  case BLE_GAP_EVENT_PER_SUBEV_RESP:
    return "BLE_GAP_EVENT_PER_SUBEV_RESP";
#endif
#ifdef BLE_GAP_EVENT_PERIODIC_TRANSFER_V2
  case BLE_GAP_EVENT_PERIODIC_TRANSFER_V2:
    return "BLE_GAP_EVENT_PERIODIC_TRANSFER_V2";
#endif
  default:
    return "BLE_GAP_EVENT_UNKNOWN";
  }
}

static const char *ble_hci_reason_to_str(uint8_t hci_reason) {
  switch (hci_reason) {
  case 0x08:
    return "Connection Timeout";
  case 0x13:
    return "Remote User Terminated Connection";
  case 0x14:
    return "Remote Device Terminated Connection (Low Resources)";
  case 0x15:
    return "Remote Device Terminated Connection (Power Off)";
  case 0x16:
    return "Connection Terminated by Local Host";
  case 0x1A:
    return "Unsupported Remote Feature";
  case 0x1F:
    return "Unspecified Error";
  case 0x3E:
    return "Connection Failed to be Established";
  default:
    return "Unknown/Other";
  }
}

// NimBLE commonly reports disconnect/status codes as 0x200 + HCI_reason.
// Example: 531 == 0x213 == 0x200 + 0x13 (Remote User Terminated Connection)
static void ble_status_to_text(int status, char *out, size_t out_len) {
  if (out_len == 0) {
    return;
  }

  if (status == 0) {
    snprintf(out, out_len, "OK");
    return;
  }

  // 0x200..0x2FF: HCI reason embedded in low byte
  if ((status & 0xFF00) == 0x0200) {
    uint8_t hci_reason = static_cast<uint8_t>(status & 0xFF);
    snprintf(out, out_len, "0x%X (HCI 0x%02X: %s)", status, hci_reason,
             ble_hci_reason_to_str(hci_reason));
    return;
  }

  // 0x00..0xFF: raw HCI reason (some paths report it directly)
  if ((status & 0xFFFFFF00) == 0) {
    uint8_t hci_reason = static_cast<uint8_t>(status & 0xFF);
    snprintf(out, out_len, "0x%X (HCI 0x%02X: %s)", status, hci_reason,
             ble_hci_reason_to_str(hci_reason));
    return;
  }

  snprintf(out, out_len, "0x%X", status);
}

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
    // Once connected, stop advertising (some clients expect this).
    if (ble_gap_adv_active()) {
      (void)ble_gap_adv_stop();
    }
  } else {
    char status_text[96];
    ble_status_to_text(status, status_text, sizeof(status_text));
    espwifi->log(WARNING, "🔵 BLE Connection failed, status=%d (%s)", status,
                 status_text);
    // If a connection attempt failed, resume advertising so device is
    // scannable.
    if (espwifi->getBLEStatus() != 0) {
      (void)espwifi->startBLEAdvertising();
    }
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

  char reason_text[96];
  ble_status_to_text(reason, reason_text, sizeof(reason_text));
  espwifi->log(INFO, "🔵 BLE Disconnected, reason=%d (%s)", reason,
               reason_text);
  // Resume advertising after disconnect so the device can be found again.
  // Skip if BLE is stopping (we mark bleStarted=false at the start of
  // deinitBLE()).
  if (espwifi->getBLEStatus() != 0) {
    (void)espwifi->startBLEAdvertising();
  }
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
  // Keep advertising unless we're connected or BLE is stopping.
  if (espwifi->getBLEStatus() != 0 && !ble_gap_conn_active()) {
    (void)espwifi->startBLEAdvertising();
  }
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
  struct ble_gap_conn_desc desc;

  // Always log the raw event type with a best-effort name, so numbered events
  // like 38/34/18 become readable in logs.
  espwifi->log(DEBUG, "🔵 BLE GAP event: %d (%s)", event->type,
               ble_gap_event_type_to_str(event->type));

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

  case BLE_GAP_EVENT_ENC_CHANGE:
    // Encryption status changed (pairing complete)
    if (event->enc_change.status == 0) {
      espwifi->log(INFO,
                   "🔵 🔐 BLE Connection encrypted (paired successfully) ✨");
    } else {
      espwifi->log(WARNING, "🔵 🔐 BLE Encryption failed: status=%d",
                   event->enc_change.status);
    }
    break;

  case BLE_GAP_EVENT_REPEAT_PAIRING:
    // Device trying to pair again - allow it
    espwifi->log(INFO, "🔵 🔐 BLE Repeat pairing request, deleting old bond");
    // Delete old bond and allow re-pairing
    ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
    ble_store_util_delete_peer(&desc.peer_id_addr);
    return BLE_GAP_REPEAT_PAIRING_RETRY;

  default:
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
