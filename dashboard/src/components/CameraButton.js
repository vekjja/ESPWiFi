import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import CameraSettingsModal from "./CameraSettingsModal";

export default function CameraButton({
  config,
  deviceOnline,
  saveConfigToDevice,
}) {
  const [modalOpen, setModalOpen] = useState(false);
  const isCameraAvailable = config?.camera?.installed !== false;
  const isDisabled = !deviceOnline || !config || !isCameraAvailable;

  const handleClick = () => {
    if (!isDisabled && config && saveConfigToDevice) {
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
            ? "Control socket disconnected"
            : !config
            ? "Loading configuration..."
            : !isCameraAvailable
            ? "Camera not installed"
            : "Camera Settings"
        }
      >
        <span>
          <Fab
            size="medium"
            color="primary"
            onClick={handleClick}
            disabled={isDisabled}
            sx={{
              color: isDisabled ? "text.disabled" : "primary.main",
              backgroundColor: isDisabled ? "action.disabled" : "action.hover",
              "&:hover": {
                backgroundColor: isDisabled
                  ? "action.disabled"
                  : "action.selected",
              },
            }}
          >
            <CameraAltIcon />
          </Fab>
        </span>
      </Tooltip>

      {modalOpen && config && saveConfigToDevice && (
        <CameraSettingsModal
          open={modalOpen}
          onClose={handleCloseModal}
          config={config}
          saveConfigToDevice={saveConfigToDevice}
          deviceOnline={deviceOnline}
        />
      )}
    </>
  );
}
