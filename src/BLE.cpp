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

#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// BLE runtime state
static bool bleStarted = false;
static bool nimbleInitialized =
    false; // Track NimBLE stack initialization separately

// ============================================================================
// Minimal GATT service
//
// NOTE (Web Bluetooth): browsers only expose services listed in
// `optionalServices` when using `acceptAllDevices: true`. Your UI already
// requests the standard Device Information service, so we use 0x180A here to
// allow "auto-discovery" from the UI without hardcoding a custom service UUID.
// ============================================================================
static const ble_uuid16_t kSvcUuid =
    BLE_UUID16_INIT(0x180A); // Device Information
static const ble_uuid16_t kChrUuid = BLE_UUID16_INIT(0xFFF1);

static bool gatt_defs_initialized = false;
static ble_gatt_chr_def gatt_svr_chrs[2];
static ble_gatt_svc_def gatt_svr_svcs[2];

static int gatt_svr_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt,
                                  void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    static const char kResp[] = "ok";
    return os_mbuf_append(ctxt->om, kResp, sizeof(kResp) - 1) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    // Accept writes; provisioning payload handling can be added later.
    return 0;
  }

  return BLE_ATT_ERR_UNLIKELY;
}

static void gatt_svr_init_defs() {
  if (gatt_defs_initialized) {
    return;
  }

  // Zero-init everything to avoid missing-field-initializer warnings and to
  // ensure forward-compatible default values across NimBLE versions.
  memset(gatt_svr_chrs, 0, sizeof(gatt_svr_chrs));
  memset(gatt_svr_svcs, 0, sizeof(gatt_svr_svcs));

  // Characteristic: READ/WRITE "ok"
  gatt_svr_chrs[0].uuid = (const ble_uuid_t *)&kChrUuid;
  gatt_svr_chrs[0].access_cb = gatt_svr_chr_access_cb;
  gatt_svr_chrs[0].arg = nullptr;
  gatt_svr_chrs[0].descriptors = nullptr;
  gatt_svr_chrs[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE;
  gatt_svr_chrs[0].min_key_size = 0;
  gatt_svr_chrs[0].val_handle = nullptr;
  gatt_svr_chrs[0].cpfd = nullptr;

  // Service: Primary 0xFFF0
  gatt_svr_svcs[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
  gatt_svr_svcs[0].uuid = (const ble_uuid_t *)&kSvcUuid;
  gatt_svr_svcs[0].includes = nullptr;
  gatt_svr_svcs[0].characteristics = gatt_svr_chrs;

  gatt_defs_initialized = true;
}

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
  uint8_t own_addr_type;

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

  // Advertise our custom service UUID so clients expecting advertised services
  // (or filtering by UUID) can find it.
  fields.uuids16 = (ble_uuid16_t *)&kSvcUuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to set advertising data, error=%d", rc);
    return ESP_FAIL;
  }

  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to infer BLE address type, rc=%d", rc);
    return ESP_FAIL;
  }

  // Start advertising - pass 'this' as arg for callbacks
  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
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
  // Early return if already started
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

  // Re-entrancy guard
  static bool initInProgress = false;
  if (initInProgress) {
    log(WARNING, "🔵 BLE initialization already in progress");
    return false;
  }
  initInProgress = true;

  log(INFO, "🔵 Starting BLE Provisioning");

  esp_err_t ret;

  // Configure the host callbacks BEFORE nimble_port_init (required)
  ble_hs_cfg.sync_cb = [](void) {
    extern ESPWiFi espwifi;
    espwifi.bleHostSyncHandler(&espwifi);
  };

  ble_hs_cfg.reset_cb = [](int reason) {
    extern ESPWiFi espwifi;
    espwifi.bleHostResetHandler(reason, &espwifi);
  };

  // Initialize NimBLE host - but only if not already initialized
  // Double-init causes ESP_ERR_INVALID_STATE
  if (!nimbleInitialized) {
    log(DEBUG, "🔵 Initializing NimBLE stack");
    ret = nimble_port_init();
    if (ret == ESP_ERR_INVALID_STATE) {
      // On some ESP-IDF versions this can mean NimBLE/controller was already
      // initialized elsewhere. Treat it as already-initialized.
      log(WARNING,
          "🔵 NimBLE port already initialized (ESP_ERR_INVALID_STATE), "
          "continuing");
    } else if (ret != ESP_OK) {
      log(ERROR, "🔵 Failed to initialize NimBLE port: %s",
          esp_err_to_name(ret));

      initInProgress = false;
      return false;
    }

    nimbleInitialized = true;

    // NimBLE initialized successfully
    // Allow time for WiFi/BT coexistence to stabilize if WiFi is running
    if (isWiFiInitialized()) {
      log(DEBUG, "🔵 WiFi coexistence: allowing stabilization period");
      vTaskDelay(pdMS_TO_TICKS(200));
      feedWatchDog();
    }
  } else {
    log(DEBUG,
        "🔵 NimBLE stack already initialized, skipping nimble_port_init");
  }

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

  // Register application services/characteristics
  gatt_svr_init_defs();
  int rc = ble_gatts_count_cfg(gatt_svr_svcs);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to count GATT services, rc=%d", rc);
    initInProgress = false;
    return false;
  }
  rc = ble_gatts_add_svcs(gatt_svr_svcs);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to add GATT services, rc=%d", rc);
    initInProgress = false;
    return false;
  }

  feedWatchDog();

  // Start the host task
  nimble_port_freertos_init(bleHostTaskStatic);

  bleStarted = true;
  initInProgress = false;
  log(INFO, "🔵 BLE Provisioning Started");

  return true;
}

/**
 * @brief Stop and deinitialize BLE provisioning
 *
 * Stops advertising, disconnects clients, and shuts down the NimBLE stack
 * and BT controller. Safe to call even if BLE is not running.
 *
 * Performs full cleanup to ensure clean restart.
 */
void ESPWiFi::deinitBLE() {
  // Early return if nothing to stop
  if (!bleStarted && !nimbleInitialized) {
    return;
  }

  log(INFO, "🔵 Stopping BLE Provisioning");

  // Prevent auto-restart advertising on disconnect while we intentionally stop.
  bleStarted = false;

  // Stop NimBLE host (ends the host task)
  esp_err_t ret = nimble_port_stop();
  if (ret != ESP_OK) {
    log(WARNING, "🔵 Failed to stop NimBLE port: %s", esp_err_to_name(ret));
    // Continue with cleanup anyway
  }

  feedWatchDog();

  // Deinitialize NimBLE (on newer ESP-IDF this owns controller/HCI lifecycle)
  if (nimbleInitialized) {
    nimble_port_deinit();
    nimbleInitialized = false;
  }

  bleStarted = false;
  log(INFO, "🔵 BLE Provisioning Stopped");
}

/**
 * @brief Get current BLE status
 *
 * @return 0 = not running, 1 = started but not advertising,
 *         2 = advertising, 3 = connected
 */
uint8_t ESPWiFi::getBLEStatus() {
  if (!bleStarted) {
    return 0; // Not running
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
 *         Returns empty string if BLE is not running
 */
std::string ESPWiFi::getBLEAddress() {
  if (!bleStarted) {
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
