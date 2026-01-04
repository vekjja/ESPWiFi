import React, { useState, useEffect } from "react";
import { Fab, Tooltip, Badge } from "@mui/material";
import {
  Bluetooth as BluetoothIcon,
  BluetoothDisabled as BluetoothDisabledIcon,
  BluetoothSearching as BluetoothSearchingIcon,
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
  const [bleStatus, setBleStatus] = useState(null);

  // Poll BLE status when device is online
  useEffect(() => {
    if (!deviceOnline) {
      setBleStatus(null);
      return;
    }

    const fetchStatus = async () => {
      try {
        const url = buildApiUrl("/api/ble/status");
        const options = getFetchOptions("GET");
        const response = await fetch(url, options);
        if (response.ok) {
          const data = await response.json();
          setBleStatus(data);
        } else {
          // Silently handle 404 - endpoint might not be available
          setBleStatus(null);
        }
      } catch (error) {
        // Silently handle errors in development when device isn't connected
        if (process.env.NODE_ENV !== "production") {
          setBleStatus(null);
        }
      }
    };

    fetchStatus();
    const interval = setInterval(fetchStatus, 3000);
    return () => clearInterval(interval);
  }, [deviceOnline]);

  const handleClick = () => {
    if (deviceOnline) {
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  const isDisabled = !deviceOnline || !config;
  const isEnabled = config?.ble?.enabled || false;
  const status = bleStatus?.status || "not_initialized";

  const getIcon = () => {
    if (!isEnabled) return <BluetoothDisabledIcon />;
    if (status === "connected") return <BluetoothConnectedIcon />;
    if (status === "advertising") return <BluetoothSearchingIcon />;
    return <BluetoothIcon />;
  };

  const getTooltipText = () => {
    if (!config) return "Loading configuration...";
    return "Web Bluetooth - Connect to BLE Devices";
  };

  const getBadgeContent = () => {
    if (status === "connected") return "●";
    if (status === "advertising") return "◉";
    return null;
  };

  return (
    <>
      <Tooltip title={getTooltipText()}>
        <Badge
          badgeContent={getBadgeContent()}
          color={status === "connected" ? "success" : "warning"}
          overlap="circular"
        >
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
            {getIcon()}
          </Fab>
        </Badge>
      </Tooltip>

      {modalOpen && (
        <BluetoothSettingsModal
          config={config}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          deviceOnline={deviceOnline}
          open={modalOpen}
          onClose={handleCloseModal}
          bleStatus={bleStatus}
        />
      )}
    </>
  );
}
