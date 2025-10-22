import React from "react";
import { Fab, Tooltip, Stack, Paper } from "@mui/material";
import {
  Settings as SettingsIcon,
  CameraAlt as CameraAltIcon,
  SignalCellularAlt as SignalCellularAltIcon,
  SignalCellularAlt1Bar as SignalCellularAlt1BarIcon,
  SignalCellularAlt2Bar as SignalCellularAlt2BarIcon,
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
  // Get the appropriate signal icon based on RSSI value
  const getRSSIIconComponent = (rssiValue) => {
    if (rssiValue === null || rssiValue === undefined) {
      return <SignalCellularAltIcon />;
    }
    if (rssiValue >= -60) return <SignalCellularAltIcon />;
    if (rssiValue >= -70) return <SignalCellularAlt2BarIcon />;
    if (rssiValue >= -80) return <SignalCellularAlt1BarIcon />;
    return <SignalCellularAltIcon />;
  };
  const buttonProps = {
    size: "medium",
    color: "primary",
  };

  const SettingsButton = ({
    onClick,
    tooltip,
    icon,
    color,
    sx = {},
    disabled = false,
  }) => {
    const button = (
      <Fab
        {...buttonProps}
        onClick={disabled ? undefined : onClick}
        disabled={disabled}
        sx={{
          color: disabled ? "text.disabled" : color || "primary.main",
          backgroundColor: disabled ? "action.disabled" : "action.hover",
          "&:hover": {
            backgroundColor: disabled ? "action.disabled" : "action.selected",
          },
          ...sx,
        }}
      >
        {icon}
      </Fab>
    );

    // Wrap disabled buttons in a span to fix MUI Tooltip warning
    if (disabled) {
      return (
        <Tooltip title={tooltip}>
          <span>{button}</span>
        </Tooltip>
      );
    }

    return <Tooltip title={tooltip}>{button}</Tooltip>;
  };

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
          disabled={!deviceOnline}
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
          disabled={!deviceOnline}
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
            ) : rssiEnabled ? (
              getRSSIIconComponent(rssiValue)
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
          disabled={!deviceOnline}
        />
        <SettingsButton
          onClick={onFileBrowser}
          tooltip="File Browser - Browse SD card and Internal files"
          icon={<FolderOpenIcon />}
          color={deviceOnline ? "primary.main" : "text.disabled"}
          disabled={!deviceOnline}
        />
        <SettingsButton
          onClick={onAddModule}
          tooltip="Add Module"
          icon={<AddIcon />}
          disabled={!deviceOnline}
        />
      </Stack>
    </Paper>
  );
}
