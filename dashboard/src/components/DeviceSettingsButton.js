import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import SettingsIcon from "@mui/icons-material/Settings";
import DeviceSettingsModal from "./DeviceSettingsModal";

export default function DeviceSettingsButton({
  config,
  deviceOnline,
  saveConfigToDevice,
  cloudMode = false,
}) {
  const [modalOpen, setModalOpen] = useState(false);

  const handleClick = () => {
    // Only open modal if device is online AND config is loaded
    if (!cloudMode && deviceOnline && config) {
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
            ? "Device Settings via cloud tunnel is not available yet"
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
            disabled={cloudMode || !deviceOnline || !config}
            sx={{
              color:
                cloudMode || !deviceOnline || !config
                  ? "text.disabled"
                  : "primary.main",
              backgroundColor:
                cloudMode || !deviceOnline || !config
                  ? "action.disabled"
                  : "action.hover",
              "&:hover": {
                backgroundColor:
                  cloudMode || !deviceOnline || !config
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
        />
      )}
    </>
  );
}
