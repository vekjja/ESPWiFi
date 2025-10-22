import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import { Settings as SettingsIcon } from "@mui/icons-material";
import DeviceSettingsModal from "./DeviceSettingsModal";

export default function DeviceSettingsButton({
  config,
  deviceOnline,
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

  return (
    <>
      <Tooltip title="Device Settings">
        <Fab
          size="medium"
          color="primary"
          onClick={handleClick}
          disabled={!deviceOnline}
          sx={{
            color: !deviceOnline ? "text.disabled" : "primary.main",
            backgroundColor: !deviceOnline ? "action.disabled" : "action.hover",
            "&:hover": {
              backgroundColor: !deviceOnline
                ? "action.disabled"
                : "action.selected",
            },
          }}
        >
          <SettingsIcon />
        </Fab>
      </Tooltip>

      {modalOpen && (
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
