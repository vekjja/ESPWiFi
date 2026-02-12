#ifndef ESPWiFi_POWER
#define ESPWiFi_POWER

#include "ESPWiFi.h"
#include "esp_wifi.h"
#include <cmath>

/**
 * @brief Applies WiFi power settings to the running WiFi driver.
 *
 * This function applies the power management settings to the WiFi hardware
 * after the WiFi driver has been initialized AND started. Both power APIs
 * require the WiFi driver to be in the started state.
 *
 * ESP-IDF Power Management Details:
 * --------------------------------
 * Transmit Power (esp_wifi_set_max_tx_power):
 * - Unit: 0.25 dBm steps
 * - Range: 8 (2 dBm) to 80 (20 dBm)
 * - Default: ~78 (19.5 dBm)
 * - Higher power = better range but more interference and power consumption
 * - Must comply with regional regulatory limits (typically ≤20 dBm)
 * - **Requires WiFi to be started** (ESP_ERR_WIFI_NOT_STARTED otherwise)
 *
 * Power Save Modes (esp_wifi_set_ps):
 * - WIFI_PS_NONE: No power saving (best performance, ~240mA active)
 * - WIFI_PS_MIN_MODEM: Minimum modem sleep (balanced, ~100mA avg)
 * - WIFI_PS_MAX_MODEM: Maximum modem sleep (lowest power, ~20mA avg)
 * - Power save primarily affects STA (client) mode
 * - In AP mode, power save has limited effect
 * - Can be called before or after start, but more reliable after
 *
 * The function follows ESP-IDF best practices:
 * - Only called when WiFi driver is started
 * - Uses graceful error handling (logs but doesn't abort)
 * - Checks return codes for all WiFi API calls
 * - Provides detailed feedback on applied settings
 *
 * @note Called from startClient() and startAP() AFTER esp_wifi_start()
 * @note Also called from powerConfigHandler() when settings change at runtime
 * @note Both APIs work best when WiFi is fully started
 * @note Requires WiFi to be enabled in configuration
 */
void ESPWiFi::applyWiFiPowerSettings() {
  // Skip if WiFi is disabled (shouldn't happen, but defensive check)
  if (!config["wifi"]["enabled"].as<bool>()) {
    return;
  }

  bool anySettingApplied = false;

  // ---- Apply Transmit Power ----
  double txPowerDbm = 19.5; // Default
  if (config["wifi"]["power"]["txPower"].is<double>() ||
      config["wifi"]["power"]["txPower"].is<int>()) {
    txPowerDbm = config["wifi"]["power"]["txPower"].as<double>();
  }

  // Validate and clamp to hardware limits
  if (txPowerDbm < 2.0) {
    log(WARNING,
        "📶🔋 WiFi Power: TX power %.1f dBm below minimum, clamping to 2 dBm",
        txPowerDbm);
    txPowerDbm = 2.0;
  } else if (txPowerDbm > 20.0 && txPowerDbm <= 21.0) {
    // Ultra mode: 20.5-21 dBm
    log(WARNING,
        "📶🔋 WiFi Power: ULTRA MODE %.1f dBm - Exceeds regulatory limits! "
        "Use only in shielded lab environment. May cause interference and "
        "hardware degradation.",
        txPowerDbm);
  } else if (txPowerDbm > 21.0) {
    // Beyond hardware maximum - clamp and warn
    log(WARNING,
        "📶🔋 WiFi Power: TX power %.1f dBm exceeds hardware maximum, "
        "clamping to 21 dBm",
        txPowerDbm);
    txPowerDbm = 21.0;
  }

  // Convert dBm to ESP-IDF units (0.25 dBm steps)
  // Round to nearest 0.25 dBm increment for precision
  int8_t txPowerQuarters = static_cast<int8_t>(std::round(txPowerDbm * 4.0));

  // Final hardware range check (defensive programming)
  // The ESP-IDF driver will also clamp, but we do it here for clarity
  if (txPowerQuarters < 8)
    txPowerQuarters = 8; // 2 dBm
  if (txPowerQuarters > 84)
    txPowerQuarters = 84; // 21 dBm (hardware maximum)

  esp_err_t err = esp_wifi_set_max_tx_power(txPowerQuarters);
  if (err != ESP_OK) {
    // Non-critical failure - WiFi will use default power
    log(WARNING, "📶🔋 WiFi Power: Failed to set TX power: %s",
        esp_err_to_name(err));
  } else {
    // Read back actual applied power from driver to verify
    int8_t appliedPower = 0;
    esp_err_t readErr = esp_wifi_get_max_tx_power(&appliedPower);
    if (readErr == ESP_OK) {
      double actualPowerDbm = appliedPower / 4.0;
      log(INFO,
          "📶🔋 WiFi Power: TX power set to %.1f dBm (requested: %.1f dBm)",
          actualPowerDbm, txPowerDbm);
      log(DEBUG, "📶🔋\tRaw driver value: %d quarter-dBm units", appliedPower);

      double difference = std::abs(actualPowerDbm - txPowerDbm);
      if (difference > 0.5) {
        log(WARNING,
            "📶🔋 WiFi Power: Applied power differs from requested by %.1f dBm "
            "(hardware limitation)",
            difference);
      }
    } else {
      // Fallback if read fails
      double requestedPower = txPowerQuarters / 4.0;
      log(INFO, "📶🔋 WiFi Power: Current TX: %.1f dBm", requestedPower);
      log(WARNING, "📶🔋\tFailed to read back power from driver: %s",
          esp_err_to_name(readErr));
    }
    anySettingApplied = true;
  }

  // ---- Apply Power Save Mode ----
  std::string powerSaveMode = "none"; // Default
  if (config["wifi"]["power"]["powerSave"].is<std::string>()) {
    powerSaveMode = config["wifi"]["power"]["powerSave"].as<std::string>();
    toLowerCase(powerSaveMode);
  }

  // Map configuration string to ESP-IDF power save type
  wifi_ps_type_t psType = WIFI_PS_NONE;
  const char *psDescription = "none (best performance)";

  if (powerSaveMode == "min" || powerSaveMode == "minimum") {
    psType = WIFI_PS_MIN_MODEM;
    psDescription = "minimum modem sleep (balanced)";
  } else if (powerSaveMode == "max" || powerSaveMode == "maximum") {
    psType = WIFI_PS_MAX_MODEM;
    psDescription = "maximum modem sleep (lowest power)";
  } else if (powerSaveMode != "none") {
    log(WARNING, "📶🔋 WiFi Power: Invalid power save mode '%s', using 'none'",
        powerSaveMode.c_str());
    // Continue with WIFI_PS_NONE (already set)
  }

  err = esp_wifi_set_ps(psType);
  if (err != ESP_OK) {
    // Log as WARNING since WiFi should be initialized at this point
    log(WARNING,
        "📶🔋 WiFi Power: Failed to set power save mode: %s (will use default)",
        esp_err_to_name(err));
  } else {
    log(INFO, "📶🔋 WiFi Power: Power Save Mode: %s", psDescription);
    anySettingApplied = true;
  }

  // ---- Summary ----
  if (!anySettingApplied) {
    log(DEBUG,
        "📶🔋 WiFi Power: Settings not applied (WiFi may not be initialized)");
  }
}

/**
 * @brief Handles runtime WiFi power configuration changes.
 *
 * This function is called from handleConfigUpdate() in the main loop whenever
 * the configuration is updated (e.g., via the web UI or API). It detects
 * changes to power settings and applies them to the running WiFi driver.
 *
 * Configuration Tracking:
 * ----------------------
 * The function uses static variables to track the previous configuration
 * allows it to detect when settings have actually changed and avoid
 * unnecessary WiFi API calls.
 *
 * Tracked Settings:
 * - wifi.power.txPower: Transmit power in dBm (2-20 dBm)
 * - wifi.power.powerSave: Power save mode (none/min/max)
 *
 * Runtime Application:
 * -------------------
 * Unlike initial power settings applied during WiFi initialization, runtime
 * changes are applied to an already-running WiFi driver. The WiFi driver
 * supports dynamic power changes without requiring a restart, though changes
 * to power save mode may cause a brief interruption in connectivity.
 *
 * The function follows the config handler pattern:
 * - Static variables track last known state
 * - Only acts when configuration actually changes
 * - Logs changes for debugging and user feedback
 * - Graceful error handling (non-blocking)
 * - Safe to call repeatedly in main loop
 *
 * @note Called from handleConfigUpdate() in the main system loop
 * @note Only applies changes if WiFi is enabled and initialized
 * @note Changes take effect immediately without WiFi restart
 * @note First run initializes tracking variables from current config
 */
void ESPWiFi::powerConfigHandler() {
  // Skip entirely if WiFi is not enabled
  if (!config["wifi"]["enabled"].as<bool>()) {
    return;
  }

  // Power save mode enum for efficient RAM usage (1 byte vs ~32 for
  // std::string)
  enum PowerSaveMode : uint8_t { PS_NONE = 0, PS_MIN = 1, PS_MAX = 2 };

  // Track previous power settings (initialized on first call)
  // Total: 8 (double) + 1 (enum) + 1 (bool) = 10 bytes (vs ~48 with string)
  static double lastTxPower = 19.5;
  static PowerSaveMode lastPowerSave = PS_NONE;
  static bool firstRun = true;

  // Get current power settings from config
  double currentTxPower = 19.5; // Default
  if (config["wifi"]["power"]["txPower"].is<double>() ||
      config["wifi"]["power"]["txPower"].is<int>()) {
    currentTxPower = config["wifi"]["power"]["txPower"].as<double>();
  }

  // Parse power save mode from config to enum
  PowerSaveMode currentPowerSave = PS_NONE; // Default
  if (config["wifi"]["power"]["powerSave"].is<std::string>()) {
    std::string psStr = config["wifi"]["power"]["powerSave"].as<std::string>();
    toLowerCase(psStr);
    if (psStr == "min" || psStr == "minimum") {
      currentPowerSave = PS_MIN;
    } else if (psStr == "max" || psStr == "maximum") {
      currentPowerSave = PS_MAX;
    }
  }

  // On first run, just initialize tracking variables without logging changes
  if (firstRun) {
    lastTxPower = currentTxPower;
    lastPowerSave = currentPowerSave;
    firstRun = false;
    return;
  }

  // Detect changes and apply if necessary
  bool txPowerChanged = (currentTxPower != lastTxPower);
  bool powerSaveChanged = (currentPowerSave != lastPowerSave);

  if (txPowerChanged || powerSaveChanged) {
    log(INFO, "📶🔋 WiFi TX Power Changed:  %.1f dBm → %.1f dBm", lastTxPower,
        currentTxPower);

    if (powerSaveChanged) {
      // Helper lambda to convert enum to string for logging
      auto psToStr = [](PowerSaveMode ps) -> const char * {
        switch (ps) {
        case PS_NONE:
          return "none";
        case PS_MIN:
          return "min";
        case PS_MAX:
          return "max";
        default:
          return "unknown";
        }
      };
      log(DEBUG, "📶🔋\tPower Save: %s → %s", psToStr(lastPowerSave),
          psToStr(currentPowerSave));
    }

    // Apply the new settings
    applyWiFiPowerSettings();

    // Update tracking variables for next comparison
    lastTxPower = currentTxPower;
    lastPowerSave = currentPowerSave;
  }
}

/**
 * @brief Retrieves current WiFi power settings and actual applied values.
 *
 * This function queries the WiFi driver for the actual TX power and power save
 * settings that are currently in effect. This is useful for:
 * - Verifying that requested settings were applied
 * - Debugging power-related issues
 * - Monitoring actual vs configured power levels
 * - Exposing power info via API endpoints
 *
 * The function returns a JSON document with:
 * - configured: Settings from config (what was requested)
 * - actual: Settings from WiFi driver (what's actually applied)
 * - chip: Chip-specific power capabilities and limitations
 * - diagnostics: Additional diagnostic information
 * - units and descriptions for user understanding
 *
 * @return JsonDocument containing power configuration and actual values
 * @note Returns empty document if WiFi is not enabled or not started
 */
JsonDocument ESPWiFi::getWiFiPowerInfo() {
  JsonDocument doc;

  // Skip if WiFi is not enabled
  if (!config["wifi"]["enabled"].as<bool>()) {
    doc["error"] = "WiFi not enabled";
    return doc;
  }

  // Get configured values
  double configuredTxPower = 19.5;
  if (config["wifi"]["power"]["txPower"].is<double>() ||
      config["wifi"]["power"]["txPower"].is<int>()) {
    configuredTxPower = config["wifi"]["power"]["txPower"].as<double>();
  }

  std::string configuredPowerSave = "none";
  if (config["wifi"]["power"]["powerSave"].is<std::string>()) {
    configuredPowerSave =
        config["wifi"]["power"]["powerSave"].as<std::string>();
    toLowerCase(configuredPowerSave);
  }

  doc["configured"]["txPower"] = configuredTxPower;
  doc["configured"]["txPowerUnit"] = "dBm";
  doc["configured"]["powerSave"] = configuredPowerSave;

  // Get actual applied values from WiFi driver
  int8_t actualTxPower = 0;
  esp_err_t err = esp_wifi_get_max_tx_power(&actualTxPower);
  if (err == ESP_OK) {
    double actualTxPowerDbm = actualTxPower / 4.0;
    doc["actual"]["txPower"] = actualTxPowerDbm;
    doc["actual"]["txPowerUnit"] = "dBm";
    doc["actual"]["txPowerRaw"] = actualTxPower; // Raw units (0.25 dBm steps)

    // Check if there's a discrepancy
    double difference = std::abs(actualTxPowerDbm - configuredTxPower);
    doc["diagnostics"]["powerDifference"] = difference;
    doc["diagnostics"]["powerDifferenceUnit"] = "dBm";

    if (difference > 0.5) {
      doc["diagnostics"]["powerDiscrepancy"] = true;
      doc["diagnostics"]["note"] = "Applied power differs from configured "
                                   "(hardware/regulatory/chip limit)";
    } else {
      doc["diagnostics"]["powerDiscrepancy"] = false;
    }
  } else {
    doc["actual"]["error"] = "Failed to read TX power";
    doc["actual"]["errorCode"] = esp_err_to_name(err);
  }

  // Get power save mode (no direct API to read back, so use configured value)
  doc["actual"]["powerSave"] = configuredPowerSave;
  doc["actual"]["powerSaveNote"] =
      "Power save mode cannot be read back from driver";

  // Get chip-specific information
#ifdef CONFIG_IDF_TARGET_ESP32C3
  doc["chip"]["model"] = "ESP32-C3";
  doc["chip"]["typical_max_power"] = "21.0 dBm @ 802.11b 1Mbps";
  doc["chip"]["typical_power_ht20_mcs7"] = "18.5 dBm @ 802.11n HT20 MCS7";
  doc["chip"]["note"] = "TX power varies by modulation rate (MCS)";
  doc["chip"]["power_variation"] = "18.5-21.0 dBm depending on data rate";
#elif CONFIG_IDF_TARGET_ESP32S3
  doc["chip"]["model"] = "ESP32-S3";
  doc["chip"]["typical_max_power"] = "20.5 dBm @ 802.11b 1Mbps";
  doc["chip"]["typical_power_ht20_mcs7"] = "19.5 dBm @ 802.11n HT20 MCS7";
#elif CONFIG_IDF_TARGET_ESP32S2
  doc["chip"]["model"] = "ESP32-S2";
  doc["chip"]["typical_max_power"] = "20.5 dBm @ 802.11b 1Mbps";
  doc["chip"]["typical_power_ht20_mcs7"] = "19.5 dBm @ 802.11n HT20 MCS7";
#else
  doc["chip"]["model"] = "ESP32";
  doc["chip"]["typical_max_power"] = "20.5 dBm @ 802.11b 1Mbps";
  doc["chip"]["typical_power_ht20_mcs7"] = "19.5 dBm @ 802.11n HT20 MCS7";
#endif

  // Get current WiFi connection info for diagnostics
  wifi_ap_record_t ap_info;
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    doc["diagnostics"]["connected"] = true;
    doc["diagnostics"]["rssi"] = ap_info.rssi;
    doc["diagnostics"]["channel"] = ap_info.primary;

    char ssid_str[33];
    memcpy(ssid_str, ap_info.ssid, 32);
    ssid_str[32] = '\0';
    doc["diagnostics"]["ssid"] = std::string(ssid_str);

    // Estimate expected output based on protocol
    if (ap_info.phy_11n) {
      doc["diagnostics"]["protocol"] = "802.11n";
      doc["diagnostics"]["expected_power_note"] =
          "HT20/HT40 typically ~18.5-19.5 dBm on ESP32-C3";
    } else if (ap_info.phy_11g) {
      doc["diagnostics"]["protocol"] = "802.11g";
      doc["diagnostics"]["expected_power_note"] =
          "54Mbps typically ~19.5-20 dBm";
    } else {
      doc["diagnostics"]["protocol"] = "802.11b";
      doc["diagnostics"]["expected_power_note"] =
          "1-11Mbps typically ~20-21 dBm";
    }
  } else {
    doc["diagnostics"]["connected"] = false;
    doc["diagnostics"]["note"] = "Not connected to AP - power info limited";
  }

  // Add helpful descriptions
  doc["info"]["txPowerRange"] = "2.0 - 20.0 dBm (software limit)";
  doc["info"]["txPowerPrecision"] = "0.25 dBm steps";
  doc["info"]["powerSaveModes"]["none"] = "Best performance (~240mA)";
  doc["info"]["powerSaveModes"]["min"] = "Balanced (~100mA avg)";
  doc["info"]["powerSaveModes"]["max"] = "Lowest power (~20mA avg)";

  // Measurement tips
  doc["diagnostics"]["measurement_tips"]["actual_output"] =
      "Measured output may be 1-3 dB lower due to: antenna mismatch, "
      "connector loss, PA efficiency, modulation scheme";
  doc["diagnostics"]["measurement_tips"]["protocol_dependent"] =
      "802.11b has highest power, 802.11n HT40 MCS7 has lowest";
  doc["diagnostics"]["measurement_tips"]["regulatory"] =
      "Actual power limited by regulatory domain and chip capabilities";

  return doc;
}

#endif // ESPWiFi_POWER
