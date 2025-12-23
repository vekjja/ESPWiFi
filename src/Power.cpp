#ifndef ESPWiFi_POWER
#define ESPWiFi_POWER

#include "ESPWiFi.h"
#include "esp_pm.h"
#include "esp_task_wdt.h"

void ESPWiFi::setMaxPower() {
  logInfo("⚡️ Power Mode: Performance");

  // Set CPU frequency to maximum based on chip variant
  // #if defined(CONFIG_IDF_TARGET_ESP32C3)
  //   setCpuFrequencyMhz(160);
  //   log("\tCPU Frequency: 160 MHz");
  // #elif defined(CONFIG_IDF_TARGET_ESP32S3)
  //   setCpuFrequencyMhz(240);
  //   log("\tCPU Frequency: 240 MHz");
  // #elif defined(CONFIG_IDF_TARGET_ESP32)
  //   setCpuFrequencyMhz(240);
  //   log("\tCPU Frequency: 240 MHz");
  // #else // Default to maximum available
  //   setCpuFrequencyMhz(240);
  //   log("\tCPU Frequency: 240 MHz (default)");
  // #endif

  // Set WiFi TX power to maximum (20.5 dBm)
  //   WiFi.setTxPower(WIFI_POWER_19_5dBm);
  //   log("\tWiFi TX Power: 19.5 dBm (max)");

  // Disable WiFi sleep mode for maximum performance
  //   WiFi.setSleep(false);
  //   log("\tWiFi Sleep: Disabled");

  // Disable power management (light sleep) for maximum performance
  // Allow frequency scaling for Bluetooth compatibility while keeping max
  // performance
  // #if defined(CONFIG_IDF_TARGET_ESP32)
  //   esp_pm_config_esp32_t pm_config;
  //   pm_config.max_freq_mhz = getCpuFrequencyMhz();
  //   pm_config.min_freq_mhz = 80; // Allow scaling down to 80MHz for
  //   compatibility pm_config.light_sleep_enable = false;
  //   esp_pm_configure(&pm_config);
  //   log("\tPower Management: Light sleep disabled, frequency scaling
  //   enabled");
  // #elif defined(CONFIG_IDF_TARGET_ESP32S3)
  //   esp_pm_config_esp32s3_t pm_config;
  //   pm_config.max_freq_mhz = getCpuFrequencyMhz();
  //   pm_config.min_freq_mhz = 80; // Allow scaling down to 80MHz for
  //   compatibility pm_config.light_sleep_enable = false;
  //   esp_pm_configure(&pm_config);
  //   log("\tPower Management: Light sleep disabled, frequency scaling
  //   enabled");
  // #elif defined(CONFIG_IDF_TARGET_ESP32C3)
  //   esp_pm_config_esp32c3_t pm_config;
  //   pm_config.max_freq_mhz = getCpuFrequencyMhz();
  //   pm_config.min_freq_mhz = 80; // Allow scaling down to 80MHz for
  //   compatibility pm_config.light_sleep_enable = false;
  //   esp_pm_configure(&pm_config);
  //   log("\tPower Management: Light sleep disabled, frequency scaling
  //   enabled");
  // #endif

  // Optimize PSRAM speed if available (ESP32-S3 and some ESP32 variants)
#if defined(CONFIG_SPIRAM_SUPPORT) || defined(CONFIG_ESP32S3_SPIRAM_SUPPORT)
  if (psramFound()) {
    logInfo("\tPSRAM: Enabled and Optimized");
  }
#endif
}

#endif // ESPWiFi_POWER
