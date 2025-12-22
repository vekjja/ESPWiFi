import React, { useState, useEffect, useCallback } from "react";
import { Fab, Tooltip } from "@mui/material";
import {
  Bluetooth as BluetoothIcon,
  BluetoothConnected as BluetoothConnectedIcon,
} from "@mui/icons-material";
import BluetoothSettingsModal from "./BluetoothSettingsModal";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";

export default function BluetoothButton({
  config,
  deviceOnline,
  saveConfig,
  saveConfigToDevice,
}) {
  const [modalOpen, setModalOpen] = useState(false);
  const [bluetoothStatus, setBluetoothStatus] = useState({
    enabled: false,
    connected: false,
    deviceName: "",
    address: "",
  });

  // Fetch Bluetooth status - memoized to prevent unnecessary re-renders
  const fetchBluetoothStatus = useCallback(async () => {
    if (!deviceOnline) return;

    try {
      const response = await fetch(
        buildApiUrl("/api/bluetooth/status"),
        getFetchOptions()
      );

      if (response.ok) {
        const data = await response.json();
        setBluetoothStatus({
          enabled: data.enabled || false,
          connected: data.connected || false,
          deviceName: data.deviceName || "",
          address: data.address || "",
        });
      }
    } catch (error) {
      console.error("Error fetching Bluetooth status:", error);
    }
  }, [deviceOnline]);

  // Initialize status from config when component mounts or config changes
  useEffect(() => {
    if (config?.bluetooth?.enabled !== undefined) {
      setBluetoothStatus((prev) => ({
        ...prev,
        enabled: config.bluetooth.enabled || false,
      }));
    }
  }, [config]);

  // Fetch status when component mounts or device comes online
  useEffect(() => {
    if (deviceOnline && config) {
      fetchBluetoothStatus();
    }
  }, [deviceOnline, config, fetchBluetoothStatus]);

  // Fetch status when modal opens (in case status changed)
  useEffect(() => {
    if (modalOpen && deviceOnline) {
      fetchBluetoothStatus();
    }
  }, [modalOpen, deviceOnline, fetchBluetoothStatus]);

  const handleClick = () => {
    if (deviceOnline) {
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  const isDisabled = !deviceOnline || !config;
  const isConnected = bluetoothStatus.enabled && bluetoothStatus.connected;
  const isEnabled = bluetoothStatus.enabled;

  const getTooltipText = () => {
    if (!config) return "Loading configuration...";
    if (!isEnabled) return "Bluetooth Disabled - Click to Enable";
    if (isConnected)
      return `Bluetooth Connected (${
        bluetoothStatus.deviceName || bluetoothStatus.address
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
          {isConnected ? <BluetoothConnectedIcon /> : <BluetoothIcon />}
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
          bluetoothStatus={bluetoothStatus}
          onStatusChange={fetchBluetoothStatus}
        />
      )}
    </>
  );
}
