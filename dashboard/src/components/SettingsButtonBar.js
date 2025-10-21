import React from "react";
import { Fab, Tooltip, Stack, Paper } from "@mui/material";
import {
  Settings as SettingsIcon,
  CameraAlt as CameraAltIcon,
  SignalCellularAlt as SignalCellularAltIcon,
  FolderOpen as FolderOpenIcon,
  Add as AddIcon,
} from "@mui/icons-material";

export default function SettingsButtonBar({
  config,
  deviceOnline,
  onNetworkSettings,
  onCameraSettings,
  onRSSISettings,
  onFileBrowser,
  onAddModule,
  // RSSI specific props
  rssiValue,
  rssiEnabled,
  rssiDisplayMode,
  getRSSIColor,
  getRSSIIcon,
  // Camera specific props
  cameraEnabled,
  getCameraColor,
}) {
  const buttonProps = {
    size: "medium",
    color: "primary",
  };

  const SettingsButton = ({ onClick, tooltip, icon, color, sx = {} }) => (
    <Tooltip title={tooltip}>
      <Fab
        {...buttonProps}
        onClick={onClick}
        sx={{
          color: color || "primary.main",
          backgroundColor: "action.hover",
          "&:hover": {
            backgroundColor: "action.selected",
          },
          ...sx,
        }}
      >
        {icon}
      </Fab>
    </Tooltip>
  );

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
        <SettingsButton
          onClick={onNetworkSettings}
          tooltip="Network & Configuration Settings"
          icon={<SettingsIcon />}
        />
        <SettingsButton
          onClick={onCameraSettings}
          tooltip={
            cameraEnabled
              ? "Camera Hardware Enabled - Click to Disable"
              : "Camera Hardware Disabled - Click to Enable"
          }
          icon={<CameraAltIcon />}
          color={getCameraColor ? getCameraColor() : "primary.main"}
        />
        <SettingsButton
          onClick={onRSSISettings}
          tooltip={
            rssiEnabled
              ? `RSSI: ${
                  rssiValue !== null
                    ? `${rssiValue} dBm`
                    : "Connected, waiting for data..."
                }`
              : "RSSI - Disabled"
          }
          icon={
            rssiEnabled &&
            rssiDisplayMode === "numbers" &&
            rssiValue !== null ? (
              rssiValue
            ) : rssiEnabled && getRSSIIcon ? (
              getRSSIIcon(rssiValue)
            ) : (
              <SignalCellularAltIcon />
            )
          }
          color={
            rssiEnabled
              ? getRSSIColor
                ? getRSSIColor(rssiValue)
                : "primary.main"
              : "text.disabled"
          }
        />
        <SettingsButton
          onClick={onFileBrowser}
          tooltip="File Browser - Browse SD card and Internal files"
          icon={<FolderOpenIcon />}
          color={deviceOnline ? "primary.main" : "text.disabled"}
        />
        <SettingsButton
          onClick={onAddModule}
          tooltip="Add Module"
          icon={<AddIcon />}
        />
      </Stack>
    </Paper>
  );
}
