import React from "react";
import { Stack, Paper } from "@mui/material";
import RSSIButton from "./RSSIButton";
import CameraButton from "./CameraButton";
import DeviceSettingsButton from "./DeviceSettingsButton";
import FileBrowserButton from "./FileBrowserButton";
import AddModuleButton from "./AddModuleButton";

export default function SettingsButtonBar({
  config,
  deviceOnline,
  onNetworkSettings,
  onCameraSettings,
  onRSSISettings,
  onFileBrowser,
  onAddModule,
  saveConfig,
  saveConfigToDevice,
  onRSSIDataChange,
  rssiDisplayMode,
  getRSSIColor,
  getRSSIIcon,
  // Camera specific props
  cameraEnabled,
  getCameraColor,
}) {
  // Both mobile and desktop use the same layout: horizontal row below header
  return (
    <Paper
      elevation={2}
      sx={{
        position: "sticky",
        top: "9vh", // Below the header
        zIndex: 1000,
        backgroundColor: "background.paper",
        borderRadius: 0,
        p: 1,
        mx: -1, // Extend to edges
      }}
    >
      <Stack
        direction="row"
        spacing={1}
        justifyContent="center"
        alignItems="center"
        sx={{ flexWrap: "wrap", gap: 1 }}
      >
        <RSSIButton
          config={config}
          deviceOnline={deviceOnline}
          onRSSISettings={onRSSISettings}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          onRSSIDataChange={onRSSIDataChange}
          rssiDisplayMode={rssiDisplayMode}
          getRSSIColor={getRSSIColor}
          getRSSIIcon={getRSSIIcon}
        />
        <DeviceSettingsButton
          config={config}
          deviceOnline={deviceOnline}
          onNetworkSettings={onNetworkSettings}
          saveConfigToDevice={saveConfigToDevice}
        />
        {config?.camera?.installed !== false && (
          <CameraButton
            config={config}
            deviceOnline={deviceOnline}
            onCameraSettings={onCameraSettings}
            cameraEnabled={cameraEnabled}
            getCameraColor={getCameraColor}
          />
        )}
        <FileBrowserButton
          config={config}
          deviceOnline={deviceOnline}
          onFileBrowser={onFileBrowser}
        />
        <AddModuleButton
          config={config}
          deviceOnline={deviceOnline}
          onAddModule={onAddModule}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
        />
      </Stack>
    </Paper>
  );
}
