import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import SettingsIcon from "@mui/icons-material/Settings";
import DeviceSettingsModal from "./DeviceSettingsModal";

export default function DeviceSettingsButton({
  config,
  deviceOnline,
  saveConfigToDevice,
  cloudMode = false,
  controlConnected = false,
  deviceInfoOverride = null,
  onRefreshConfig, // Add callback to refresh config
}) {
  const [modalOpen, setModalOpen] = useState(false);

  const handleClick = () => {
    // Only open modal if device is online AND config is loaded
    if ((cloudMode ? controlConnected : deviceOnline) && config) {
      // Refresh config before opening modal
      if (onRefreshConfig) {
        onRefreshConfig();
      }
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  return (
    <>
      <Tooltip
        title={
          cloudMode
            ? !controlConnected
              ? "Control tunnel not connected"
              : !config
              ? "Loading configuration..."
              : "Device Settings"
            : !deviceOnline
            ? "Device Offline"
            : !config
            ? "Loading configuration..."
            : "Device Settings"
        }
      >
        <span>
          <Fab
            size="medium"
            color="primary"
            onClick={handleClick}
            disabled={
              (cloudMode ? !controlConnected : !deviceOnline) || !config
            }
            sx={{
              color:
                (cloudMode ? !controlConnected : !deviceOnline) || !config
                  ? "text.disabled"
                  : "primary.main",
              backgroundColor:
                (cloudMode ? !controlConnected : !deviceOnline) || !config
                  ? "action.disabled"
                  : "action.hover",
              "&:hover": {
                backgroundColor:
                  (cloudMode ? !controlConnected : !deviceOnline) || !config
                    ? "action.disabled"
                    : "action.selected",
              },
            }}
          >
            <SettingsIcon />
          </Fab>
        </span>
      </Tooltip>

      {modalOpen && config && (
        <DeviceSettingsModal
          open={modalOpen}
          onClose={handleCloseModal}
          config={config}
          saveConfigToDevice={saveConfigToDevice}
          cloudMode={cloudMode}
          deviceInfoOverride={deviceInfoOverride}
        />
      )}
    </>
  );
}
