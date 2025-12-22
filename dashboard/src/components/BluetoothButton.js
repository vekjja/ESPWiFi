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
  // Show connected if Web Bluetooth is connected OR if config shows a connection
  const isConnected =
    isEnabled && (webBleConnected || config?.bluetooth?.connected || false);
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
          setWebBleConnected={setWebBleConnected}
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
