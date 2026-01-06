import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import NoPhotographyIcon from "@mui/icons-material/NoPhotography";
import CameraSettingsModal from "./CameraSettingsModal";

export default function CameraButton({
  config,
  deviceOnline,
  saveConfigToDevice,
  cameraEnabled,
  getCameraColor,
}) {
  const [modalOpen, setModalOpen] = useState(false);
  const isCameraAvailable = config?.camera?.installed !== false;
  const isDisabled = !deviceOnline || !config || !isCameraAvailable;

  const handleClick = () => {
    if (!isDisabled) {
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
          !config
            ? "Loading configuration..."
            : !isCameraAvailable
            ? "Camera not installed"
            : cameraEnabled
            ? "Camera Hardware Enabled - Click to Configure"
            : "Camera Hardware Disabled - Click to Configure"
        }
      >
        <Fab
          size="medium"
          color="primary"
          onClick={handleClick}
          disabled={isDisabled}
          sx={{
            color: isDisabled
              ? "text.disabled"
              : getCameraColor
              ? getCameraColor()
              : "primary.main",
            backgroundColor: isDisabled ? "action.disabled" : "action.hover",
            "&:hover": {
              backgroundColor: isDisabled
                ? "action.disabled"
                : "action.selected",
            },
          }}
        >
          {!isDisabled && !cameraEnabled ? (
            <NoPhotographyIcon />
          ) : (
            <CameraAltIcon />
          )}
        </Fab>
      </Tooltip>

      {modalOpen && (
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
