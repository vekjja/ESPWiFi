/**
 * @file BLE.cpp
 * @brief BLE (Bluetooth Low Energy) provisioning subsystem for ESPWiFi
 *
 * This module provides BLE-based WiFi provisioning using ESP-IDF's NimBLE
 * stack. When WiFi connection fails or when in AP mode, BLE provisioning can be
 * started to allow mobile apps to configure WiFi credentials securely.
 *
 * Key features:
 * - NimBLE-based BLE stack (lightweight alternative to Bluedroid)
 * - Secure pairing with passkey authentication
 * - Auto-start on WiFi failure (configurable)
 * - HTTP API for manual control (start/stop/status)
 * - Thread-safe initialization and cleanup
 *
 * @note BLE functionality requires CONFIG_BT_NIMBLE_ENABLED build flag
 * @note Cannot run simultaneously with Classic Bluetooth (A2DP)
 */

#ifndef ESPWiFi_BLE_H
#define ESPWiFi_BLE_H
#include "ESPWiFi.h"

#ifdef CONFIG_BT_NIMBLE_ENABLED

#include "esp_bt.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// BLE runtime state
static bool bleStarted = false;
static bool bleInitialized = false;

/**
 * @brief Start BLE advertising
 *
 * Configures and starts BLE advertising with device name and provisioning
 * service. This is a public method that can be called from handlers.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ESPWiFi::startBLEAdvertising() {
  struct ble_gap_adv_params adv_params = {};
  struct ble_hs_adv_fields fields = {};
  const char *device_name;
  int rc;

  // Set advertising parameters
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Undirected connectable
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // General discoverable

  // Set advertising data
  memset(&fields, 0, sizeof(fields));
  device_name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)device_name;
  fields.name_len = strlen(device_name);
  fields.name_is_complete = 1;

  // Set the advertising flags
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to set advertising data, error=%d", rc);
    return ESP_FAIL;
  }

  // Start advertising - pass 'this' as arg for callbacks
  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params,
                         bleGapEventCallbackStatic, this);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to start advertising, error=%d", rc);
    return ESP_FAIL;
  }

  log(INFO, "🔵 BLE Advertising started");
  return ESP_OK;
}

/**
 * @brief Initialize and start BLE provisioning
 *
 * Initializes the NimBLE stack, configures GATT services, and starts
 * advertising. This function is idempotent - calling it multiple times is safe.
 *
 * @return true if BLE is started (or was already running), false on failure
 *
 * @note Does not abort on failure per ESP32 robustness best practices
 * @note Cannot run simultaneously with Classic Bluetooth (A2DP)
 */
bool ESPWiFi::startBLE() {
  if (bleStarted) {
    log(DEBUG, "🔵 BLE Already running");
    return true;
  }

#ifdef CONFIG_BT_A2DP_ENABLE
  // Check if Bluetooth A2DP is running (mutual exclusion)
  if (btStarted) {
    log(WARNING, "🔵 Cannot start BLE: Bluetooth A2DP is running");
    return false;
  }
#endif

  log(INFO, "🔵 Starting BLE Provisioning");

  esp_err_t ret;

  // Initialize BT controller only once
  if (!bleInitialized) {
    // Release Classic BT memory if not using it
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      log(ERROR, "🔵 Failed to release BT Classic memory: %s",
          esp_err_to_name(ret));
      // Continue anyway, this is not fatal
    }

    // Initialize BT controller for BLE
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
      log(ERROR, "🔵 Failed to initialize BT controller: %s",
          esp_err_to_name(ret));
      return false;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret != ESP_OK) {
      log(ERROR, "🔵 Failed to enable BT controller: %s", esp_err_to_name(ret));
      esp_bt_controller_deinit();
      return false;
    }

    bleInitialized = true;
    feedWatchDog(); // Yield after controller init
  }

  // Initialize NimBLE host
  ret = nimble_port_init();
  if (ret != ESP_OK) {
    log(ERROR, "🔵 Failed to initialize NimBLE port: %s", esp_err_to_name(ret));
    return false;
  }

  // Configure the host callbacks - pass 'this' as arg
  ble_hs_cfg.sync_cb = [](void) {
    // We need to get the ESPWiFi instance from somewhere
    // The sync callback doesn't receive user arg, so we use a wrapper
    extern ESPWiFi espwifi; // Reference to global instance
    espwifi.bleHostSyncHandler(&espwifi);
  };

  ble_hs_cfg.reset_cb = [](int reason) {
    // Same issue - no user arg in reset callback
    extern ESPWiFi espwifi;
    espwifi.bleHostResetHandler(reason, &espwifi);
  };

  // Set device name from config
  std::string deviceName = config["deviceName"].as<std::string>();
  ret = ble_svc_gap_device_name_set(deviceName.c_str());
  if (ret != 0) {
    log(WARNING, "🔵 Failed to set BLE device name: %d", ret);
    // Continue anyway, not fatal
  }

  // Initialize GATT services
  ble_svc_gap_init();
  ble_svc_gatt_init();

  feedWatchDog(); // Yield after service init

  // Start the host task - pass 'this' as parameter
  nimble_port_freertos_init(bleHostTaskStatic);

  bleStarted = true;
  log(INFO, "🔵 BLE Provisioning Started");

  return true;
}

/**
 * @brief Stop and deinitialize BLE provisioning
 *
 * Stops advertising, disconnects clients, and shuts down the NimBLE stack.
 * Safe to call even if BLE is not running.
 *
 * @note Does not deinit the BT controller to allow quick restart
 */
void ESPWiFi::deinitBLE() {
  if (!bleStarted) {
    return;
  }

  log(INFO, "🔵 Stopping BLE Provisioning");

  // Stop NimBLE host (this will end the host task)
  esp_err_t ret = nimble_port_stop();
  if (ret != ESP_OK) {
    log(WARNING, "🔵 Failed to stop NimBLE port: %s", esp_err_to_name(ret));
    // Continue with cleanup anyway
  }

  feedWatchDog(); // Yield during shutdown

  // Deinitialize NimBLE
  nimble_port_deinit();

  bleStarted = false;
  log(INFO, "🔵 BLE Provisioning Stopped");

  // Note: We don't deinit the BT controller here to allow quick restart.
  // Full cleanup (esp_bt_controller_disable/deinit) would be done only
  // if we need to switch to Classic BT or free all BT resources.
}

/**
 * @brief Get current BLE status
 *
 * @return 0 = not initialized, 1 = initialized but not advertising,
 *         2 = advertising, 3 = connected
 */
uint8_t ESPWiFi::getBLEStatus() {
  if (!bleInitialized) {
    return 0; // Not initialized
  }

  if (!bleStarted) {
    return 1; // Initialized but not running
  }

  // Check if we have any active connections
  if (ble_gap_conn_active()) {
    return 3; // Connected
  }

  // Check if advertising is active
  if (ble_gap_adv_active()) {
    return 2; // Advertising
  }

  return 1; // Started but not advertising yet
}

/**
 * @brief Get BLE MAC address
 *
 * @return BLE MAC address as string (e.g., "AA:BB:CC:DD:EE:FF")
 *         Returns empty string if BLE is not initialized
 */
std::string ESPWiFi::getBLEAddress() {
  if (!bleInitialized) {
    return "";
  }

  uint8_t addr_type;
  uint8_t addr[6] = {0};

  int rc = ble_hs_id_infer_auto(0, &addr_type);
  if (rc != 0) {
    log(WARNING, "🔵 Failed to infer address type: %d", rc);
    return "";
  }

  rc = ble_hs_id_copy_addr(addr_type, addr, NULL);
  if (rc != 0) {
    log(WARNING, "🔵 Failed to get BLE address: %d", rc);
    return "";
  }

  char addr_str[18];
  snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x", addr[0],
           addr[1], addr[2], addr[3], addr[4], addr[5]);

  return std::string(addr_str);
}

#endif // CONFIG_BT_NIMBLE_ENABLED
/**
 * @brief Handle BLE configuration changes
 *
 * Called from handleConfigUpdate() to respond to BLE config changes.
 * Starts or stops BLE based on the "enabled" flag in config.
 */
void ESPWiFi::bleConfigHandler() {
#ifdef CONFIG_BT_NIMBLE_ENABLED

  static bool lastEnabled = config["ble"]["enabled"].as<bool>();
  bool currentEnabled = configUpdate["ble"]["enabled"].isNull()
                            ? lastEnabled
                            : configUpdate["ble"]["enabled"].as<bool>();

  if (currentEnabled != lastEnabled) {
    if (currentEnabled) {
      startBLE();
    } else {
      deinitBLE();
    }
    lastEnabled = currentEnabled;
  }
  feedWatchDog();
#endif
}

#endif // ESPWiFi_BLE_H
