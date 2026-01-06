import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import SettingsIcon from "@mui/icons-material/Settings";
import DeviceSettingsModal from "./DeviceSettingsModal";

export default function DeviceSettingsButton({
  config,
  deviceOnline,
  saveConfigToDevice,
}) {
  const [modalOpen, setModalOpen] = useState(false);

  const handleClick = () => {
    // Only open modal if device is online AND config is loaded
    if (deviceOnline && config) {
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
          !deviceOnline
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
            disabled={!deviceOnline || !config}
            sx={{
              color:
                !deviceOnline || !config ? "text.disabled" : "primary.main",
              backgroundColor:
                !deviceOnline || !config ? "action.disabled" : "action.hover",
              "&:hover": {
                backgroundColor:
                  !deviceOnline || !config
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
