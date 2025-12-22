import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import {
  Bluetooth as BluetoothIcon,
  BluetoothConnected as BluetoothConnectedIcon,
  BluetoothDisabled as BluetoothDisabledIcon,
} from "@mui/icons-material";
import BluetoothSettingsModal from "./BluetoothSettingsModal";

export default function BluetoothButton({
  config,
  deviceOnline,
  saveConfig,
  saveConfigToDevice,
}) {
  const [modalOpen, setModalOpen] = useState(false);

  const handleClick = () => {
    if (deviceOnline) {
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  const isDisabled = !deviceOnline || !config;
  const isEnabled = config?.bluetooth?.enabled || false;
  const isConnected = isEnabled && (config?.bluetooth?.connected || false);
  const deviceName = config?.deviceName || "";
  const address = config?.bluetooth?.address || "";

  const getTooltipText = () => {
    if (!config) return "Loading configuration...";
    if (!isEnabled) return "Bluetooth Disabled - Click to Enable";
    if (isConnected)
      return `Bluetooth Connected (${
        deviceName || address || "Multiple devices"
      })`;
    return "Bluetooth Enabled - Click to Configure";
  };

  return (
    <>
      <Tooltip title={getTooltipText()}>
        <Fab
          size="medium"
          color="primary"
          onClick={handleClick}
          disabled={isDisabled}
          sx={{
            color: isDisabled
              ? "text.disabled"
              : isEnabled
              ? "primary.main"
              : "text.disabled",
            backgroundColor: isDisabled ? "action.disabled" : "action.hover",
            "&:hover": {
              backgroundColor: isDisabled
                ? "action.disabled"
                : "action.selected",
            },
          }}
        >
          {isConnected ? (
            <BluetoothConnectedIcon />
          ) : isEnabled ? (
            <BluetoothIcon />
          ) : (
            <BluetoothDisabledIcon />
          )}
        </Fab>
      </Tooltip>

      {modalOpen && (
        <BluetoothSettingsModal
          config={config}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          deviceOnline={deviceOnline}
          open={modalOpen}
          onClose={handleCloseModal}
        />
      )}
    </>
  );
}
