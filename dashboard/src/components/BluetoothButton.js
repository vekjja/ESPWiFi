import React, { useEffect, useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import {
  Bluetooth as BluetoothIcon,
  BluetoothConnected as BluetoothConnectedIcon,
  BluetoothDisabled as BluetoothDisabledIcon,
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
  const [btStatus, setBtStatus] = useState(null);

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
  const isConnected = !!btStatus?.connected;
  const isConnecting = !!btStatus?.connecting;
  const targetName =
    btStatus?.targetName || config?.bluetooth?.audio?.targetName || "";

  useEffect(() => {
    let interval = null;
    let aborted = false;

    const fetchStatus = async () => {
      if (!deviceOnline || !isEnabled) {
        setBtStatus(null);
        return;
      }
      try {
        const response = await fetch(
          buildApiUrl("/api/bluetooth/status"),
          getFetchOptions()
        );
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        const data = await response.json();
        if (!aborted) {
          setBtStatus(data);
        }
      } catch (e) {
        if (!aborted) {
          // Status endpoint may be unavailable early in boot; keep UI usable.
          setBtStatus(null);
        }
      }
    };

    fetchStatus();
    interval = setInterval(fetchStatus, 2000);

    return () => {
      aborted = true;
      if (interval) clearInterval(interval);
    };
  }, [deviceOnline, isEnabled]);

  const getTooltipText = () => {
    if (!config) return "Loading configuration...";
    if (!isEnabled) return "Bluetooth Disabled - Click to Configure";
    if (isConnected) return `Bluetooth Connected (${targetName || "Speaker"})`;
    if (isConnecting)
      return `Bluetooth Connecting (${targetName || "Speaker"})`;
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
              : isConnecting
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
        />
      )}
    </>
  );
}
