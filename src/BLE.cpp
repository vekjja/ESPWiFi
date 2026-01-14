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

#include "esp_app_desc.h"

#include <ArduinoJson.h>

#include "os/os_mbuf.h"

#include "GattServiceDef.h"
#include "GattServices.h"

// BLE runtime state
static bool bleStarted = false;
static bool nimbleInitialized =
    false; // Track NimBLE stack initialization separately

// Encapsulated GATT service definitions + callbacks.
static GattServices gattServices;

// ============================================================================
// Standard Device Information Service (0x180A) characteristics
// ============================================================================
namespace {

// ---- ESPWiFi control characteristic request/response
// Helpers
static void sendJsonNotify(uint16_t conn_handle, uint16_t attr_handle,
                           const JsonDocument &doc) {
  if (conn_handle == 0 || attr_handle == 0) {
    return;
  }

  // Increased buffer size to accommodate larger responses like config
  // BLE MTU can be negotiated up to 512 bytes (including overhead)
  char buf[480]; // Leave some room for BLE protocol overhead
  size_t len = serializeJson(doc, buf, sizeof(buf));
  if (len == 0 || len >= sizeof(buf)) {
    const char *err = "{\"ok\":false,\"error\":\"resp_too_large\"}";
    len = strlen(err);
    memcpy(buf, err, len);
  }

  struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
  if (om) {
    (void)ble_gatts_notify_custom(conn_handle, attr_handle, om);
  }
}

static bool readOmToBuf(struct os_mbuf *om, char *out, size_t outCap,
                        size_t *outLen) {
  if (!out || outCap == 0)
    return false;
  if (!om) {
    out[0] = '\0';
    if (outLen)
      *outLen = 0;
    return true;
  }
  uint16_t pktLen = OS_MBUF_PKTLEN(om);
  if (pktLen >= outCap) {
    pktLen = static_cast<uint16_t>(outCap - 1);
  }
  int rc = os_mbuf_copydata(om, 0, pktLen, out);
  if (rc != 0) {
    out[0] = '\0';
    if (outLen)
      *outLen = 0;
    return false;
  }
  out[pktLen] = '\0';
  if (outLen)
    *outLen = pktLen;
  return true;
}

// BLE Control characteristic handler
static int bleControlHandler(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (!arg || !ctxt || ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  ESPWiFi *espwifi = static_cast<ESPWiFi *>(arg);

  JsonDocument resp;
  resp["ok"] = false;

  // Parse JSON command
  char reqBuf[320];
  size_t reqLen = 0;
  if (!readOmToBuf(ctxt->om, reqBuf, sizeof(reqBuf), &reqLen) || reqLen == 0) {
    resp["error"] = "empty";
    sendJsonNotify(conn_handle, attr_handle, resp);
    return BLE_ATT_ERR_UNLIKELY;
  }

  JsonDocument req;
  DeserializationError err = deserializeJson(req, reqBuf, reqLen);
  if (err) {
    resp["error"] = "bad_json";
    sendJsonNotify(conn_handle, attr_handle, resp);
    return BLE_ATT_ERR_UNLIKELY;
  }

  const char *cmd = req["cmd"];
  espwifi->log(INFO, "🔵 BLE cmd='%s'", cmd ? cmd : "(none)");

  // Commands
  if (strcmp(cmd, "get_info") == 0) {
    // Full config is often too large for BLE. Return essential fields only.
    resp["ok"] = true;
    JsonObject cfg = resp["config"].to<JsonObject>();
    cfg["deviceName"] = espwifi->config["deviceName"];
    cfg["hostname"] = espwifi->config["hostname"];
    cfg["wifi"]["mode"] = espwifi->config["wifi"]["mode"];
    cfg["wifi"]["started"] = espwifi->config["wifi"]["started"];
    cfg["wifi"]["client"]["ssid"] = espwifi->config["wifi"]["client"]["ssid"];
    cfg["wifi"]["client"]["password"] =
        espwifi->config["wifi"]["client"]["password"];
    sendJsonNotify(conn_handle, attr_handle, resp);
    return 0;
  }

  if (strcmp(cmd, "set_wifi") == 0) {
    // Alias for set_wifi_credentials (more consistent naming)
    const char *ssid = req["ssid"];
    const char *password = req["password"];
    if (!ssid) {
      resp["error"] = "missing_ssid";
      sendJsonNotify(conn_handle, attr_handle, resp);
      return BLE_ATT_ERR_UNLIKELY;
    }
    espwifi->config["wifi"]["ssid"] = ssid;
    espwifi->config["wifi"]["mode"] = "client";
    espwifi->config["wifi"]["password"] = password ? password : "";
    espwifi->log(INFO, "🔵 BLE WiFi set: %s", ssid);
    bool saved = espwifi->saveConfig();
    if (!saved) {
      resp["error"] = "failed_to_save_config";
      sendJsonNotify(conn_handle, attr_handle, resp);
      return BLE_ATT_ERR_UNLIKELY;
    }
    resp["ok"] = true;
    sendJsonNotify(conn_handle, attr_handle, resp);
    espwifi->stopWiFi();
    espwifi->feedWatchDog(99);
    espwifi->startWiFi();
    return 0;
  }

  resp["error"] = "unknown_cmd";
  sendJsonNotify(conn_handle, attr_handle, resp);
  return 0;
}

} // namespace

// ============================================================================
// ESPWiFi BLE GATT registry
// ============================================================================

void ESPWiFi::startBLEServices() {
  clearBleServices();

  const uint16_t disServiceUUID = 0x180A;
  (void)registerBleService16(disServiceUUID);

  // Control characteristic - write with notify for responses
  // Pass 'this' as arg so handler can access the ESPWiFi instance
  (void)addBleCharacteristic16(
      disServiceUUID, GattServices::controlCharUUID.value,
      BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY, bleControlHandler, this);
}

bool ESPWiFi::registerBleService16(uint16_t svcUuid16) {
  // Changing the registry affects next BLE start; safe to call any time.
  return gattServices.registerService16(svcUuid16);
}

bool ESPWiFi::unregisterBleService16(uint16_t svcUuid16) {
  return gattServices.unregisterService16(svcUuid16);
}

bool ESPWiFi::addBleCharacteristic16(uint16_t svcUuid16, uint16_t chrUuid16,
                                     uint16_t flags, BleAccessCallback accessCb,
                                     void *arg, uint8_t minKeySize) {
  return gattServices.addCharacteristic16(svcUuid16, chrUuid16, flags, accessCb,
                                          arg, minKeySize);
}

void ESPWiFi::clearBleServices() { gattServices.clear(); }

bool ESPWiFi::applyBleServiceRegistry(bool restartNow) {
  if (!restartNow) {
    return true;
  }

  // Restart BLE only if it is currently running/initialized.
  const bool wasRunning = (getBLEStatus() != 0);
  if (wasRunning) {
    deinitBLE();
  }
  // If config says BLE should be enabled, restart it; otherwise leave stopped.
  if (config["ble"]["enabled"].as<bool>()) {
    return startBLE();
  }
  return true;
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

  // Advertise a best-effort list of 16-bit service UUIDs from the registry.
  // (Truncated for ADV payload size.)
  size_t advUuidCount = 0;
  const ble_uuid16_t *advUuids =
      gattServices.advertisedUuids16(&advUuidCount, this);
  if (advUuidCount > 0) {
    fields.uuids16 = (ble_uuid16_t *)advUuids;
    fields.num_uuids16 = advUuidCount;
    fields.uuids16_is_complete = 1;
  }

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
       // =======================================================
/**
 * @brief Handle BLE configuration changes
 *
 * Called from handleConfigUpdate() to respond to BLE config changes.
 * Starts or stops BLE based on the "enabled" flag in config.
 */
void ESPWiFi::bleConfigHandler() {
#ifdef CONFIG_BT_NIMBLE_ENABLED
  // For now, BLE should start every boot and remain available for pairing /
  // provisioning, regardless of config changes. So we ignore config.ble.enabled
  // here and simply ensure BLE is running.
  if (getBLEStatus() == 0) {
    startBLE();
  }
  feedWatchDog();
#endif
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
#ifdef CONFIG_BT_NIMBLE_ENABLED
  // Early return if already started
  if (bleStarted) {
    log(DEBUG, "🔵 BLE Already running");
    return true;
  }

  log(INFO, "🔵 Starting Bluetooth Low Energy");

  esp_err_t ret;

  // Configure the host callbacks BEFORE nimble_port_init (required)
  ble_hs_cfg.sync_cb = [](void) {
    extern ESPWiFi espwifi;
    espwifi.log(INFO, "🔵 BLE Host and Controller synced");
    espwifi.log(INFO, "🔵 BLE Address: %s", espwifi.getBLEAddress().c_str());
    espwifi.startBLEAdvertising();
  };

  ble_hs_cfg.reset_cb = [](int reason) {
    extern ESPWiFi espwifi;
    espwifi.log(WARNING, "🔵 BLE Host reset, reason=%d", reason);
  };

  // Configure BLE security for encrypted connections
  // "Just Works" pairing - no PIN required, but connection is encrypted
  ble_hs_cfg.sm_bonding = 1; // Enable bonding (stores keys)
  ble_hs_cfg.sm_mitm = 0;    // No Man-in-the-Middle protection (Just Works)
  ble_hs_cfg.sm_sc = 1;      // Secure Connections (LE Secure Connections)
  ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; // Just Works pairing
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;

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
      return false;
    }

    nimbleInitialized = true;
    log(INFO, "🔵 🔐 BLE Security: Just Works pairing enabled (encrypted "
              "connection)");

    // NimBLE initialized successfully
    // Allow time for WiFi/BT coexistence to stabilize if WiFi is running
    if (isWiFiInitialized()) {
      log(DEBUG, "🔵 WiFi coexistence: allowing stabilization period");
      feedWatchDog(200);
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

  // register services/characteristics.
  startBLEServices();

  // Register application services/characteristics
  const ble_gatt_svc_def *svcs = gattServices.serviceDefs(this);
  int rc = ble_gatts_count_cfg(svcs);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to count GATT services, rc=%d", rc);
    return false;
  }
  rc = ble_gatts_add_svcs(svcs);
  if (rc != 0) {
    log(ERROR, "🔵 Failed to add GATT services, rc=%d", rc);
    return false;
  }

  feedWatchDog();

  // Start the host task
  nimble_port_freertos_init(bleHostTaskStatic);

  bleStarted = true;
  log(INFO, "🔵 BLE Initialization complete (advertising will start when host "
            "syncs)");

  return true;
#else
  return false;
#endif // CONFIG_BT_NIMBLE_ENABLED
}

#endif // ESPWiFi_BLE_H
