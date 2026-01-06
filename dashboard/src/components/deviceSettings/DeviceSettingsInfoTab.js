/**
 * @file DeviceSettingsInfoTab.js
 * @brief Comprehensive device information and configuration tab component
 *
 * This component displays and allows editing of all device settings using
 * modular, reusable card components.
 *
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html
 */

import React from "react";
import { Box, Alert, Typography, Grid, Skeleton } from "@mui/material";
import NetworkInfoCard from "./NetworkInfoCard";
import ChipInfoCard from "./ChipInfoCard";
import WifiConfigInfoCard from "./WifiConfigInfoCard";
import AuthInfoCard from "./AuthInfoCard";
import MemoryInfoCard from "./MemoryInfoCard";
import StorageInfoCard from "./StorageInfoCard";
import WifiPowerInfoCard from "./WifiPowerInfoCard";
import JsonConfigCard from "./JsonConfigCard";

/**
 * DeviceSettingsInfoTab Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.deviceInfo - Device information from /api/info endpoint
 * @param {Object} props.config - Device configuration object
 * @param {Function} props.saveConfigToDevice - Function to save configuration changes
 * @param {boolean} props.infoLoading - Loading state for device info
 * @param {string} props.infoError - Error message if info fetch failed
 * @returns {JSX.Element} The rendered Info tab component
 */
export default function DeviceSettingsInfoTab({
  deviceInfo,
  config,
  saveConfigToDevice,
  infoLoading,
  infoError,
}) {
  // ====== Render Loading State ======
  if (infoLoading) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Box sx={{ textAlign: "center" }}>
          <Skeleton
            variant="circular"
            width={40}
            height={40}
            sx={{ mx: "auto", mb: 2 }}
          />
          <Skeleton variant="text" width={200} height={24} sx={{ mb: 1 }} />
          <Skeleton variant="text" width={150} height={20} />
        </Box>
      </Box>
    );
  }

  // ====== Render Error State ======
  if (infoError) {
    return (
      <Alert severity="error" sx={{ marginBottom: 2 }}>
        {infoError}
      </Alert>
    );
  }

  // ====== Render No Data State ======
  if (!deviceInfo) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Typography color="text.secondary">
          No device information available
        </Typography>
      </Box>
    );
  }

  // ====== Render Config Loading State ======
  if (!config) {
    return (
      <Box
        sx={{
          display: "flex",
          justifyContent: "center",
          alignItems: "center",
          minHeight: "200px",
        }}
      >
        <Typography variant="body1" color="text.secondary">
          Loading configuration...
        </Typography>
      </Box>
    );
  }

  // ====== Main Render ======
  return (
    <Grid container spacing={2} sx={{ pb: 4, width: "100%", m: 0 }}>
      {/* Network Information */}
      <NetworkInfoCard deviceInfo={deviceInfo} />

      {/* WiFi Configuration */}
      <WifiConfigInfoCard
        config={config}
        deviceInfo={deviceInfo}
        onSave={saveConfigToDevice}
      />

      {/* Authentication */}
      <AuthInfoCard config={config} onSave={saveConfigToDevice} />

      {/* Memory */}
      <MemoryInfoCard deviceInfo={deviceInfo} loading={infoLoading} />

      {/* WiFi Power */}
      <WifiPowerInfoCard
        config={config}
        deviceInfo={deviceInfo}
        onSave={saveConfigToDevice}
      />

      {/* Storage */}
      <StorageInfoCard deviceInfo={deviceInfo} loading={infoLoading} />

      {/* Chip */}
      <ChipInfoCard deviceInfo={deviceInfo} />

      {/* JSON Configuration Editor */}
      <JsonConfigCard config={config} onSave={saveConfigToDevice} />
    </Grid>
  );
}
