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
  // Lift Web Bluetooth connection state to parent so it persists across modal open/close
  const [webBleConnected, setWebBleConnected] = useState(false);
  const [webBleDevice, setWebBleDevice] = useState(null);
  const [webBleServer, setWebBleServer] = useState(null);
  const [webBleService, setWebBleService] = useState(null);
  const [webBleTxCharacteristic, setWebBleTxCharacteristic] = useState(null);
  const [webBleRxCharacteristic, setWebBleRxCharacteristic] = useState(null);
  // Track if we just disconnected to prevent showing "remote" color briefly
  const [justDisconnectedWebBle, setJustDisconnectedWebBle] = useState(false);

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
  // Determine connection state:
  // - If Web Bluetooth is connected, it's a local connection (primary color)
  // - If config shows connected but Web Bluetooth is not, it's remote (warning color)
  // - Prioritize Web Bluetooth state to avoid flashing warning color when disconnecting
  const isConnected =
    isEnabled && (webBleConnected || config?.bluetooth?.connected || false);
  // Check if it's a remote connection (connected but not via Web Bluetooth)
  // Don't show remote if we just disconnected Web Bluetooth to prevent warning color flash
  const isRemoteConnected =
    isEnabled &&
    config?.bluetooth?.connected &&
    !webBleConnected &&
    !justDisconnectedWebBle;
  const deviceName = config?.deviceName || "";
  const address = config?.bluetooth?.address || "";

  const getTooltipText = () => {
    if (!config) return "Loading configuration...";
    if (!isEnabled) return "Bluetooth Disabled - Click to Configure";
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
              : isRemoteConnected
              ? "warning.main"
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
          {(() => {
            if (isConnected) {
              return <BluetoothConnectedIcon />;
            } else if (isEnabled) {
              return <BluetoothIcon />;
            } else {
              return <BluetoothDisabledIcon />;
            }
          })()}
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
          webBleConnected={webBleConnected}
          setWebBleConnected={(value) => {
            setWebBleConnected(value);
            // Track when we disconnect to prevent showing remote color
            if (!value && webBleConnected) {
              setJustDisconnectedWebBle(true);
              // Clear the flag after a short delay to allow config to update
              setTimeout(() => setJustDisconnectedWebBle(false), 2000);
            }
          }}
          webBleDevice={webBleDevice}
          setWebBleDevice={setWebBleDevice}
          webBleServer={webBleServer}
          setWebBleServer={setWebBleServer}
          webBleService={webBleService}
          setWebBleService={setWebBleService}
          webBleTxCharacteristic={webBleTxCharacteristic}
          setWebBleTxCharacteristic={setWebBleTxCharacteristic}
          webBleRxCharacteristic={webBleRxCharacteristic}
          setWebBleRxCharacteristic={setWebBleRxCharacteristic}
        />
      )}
    </>
  );
}
