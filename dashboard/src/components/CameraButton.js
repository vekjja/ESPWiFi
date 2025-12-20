import React from "react";
import { Fab, Tooltip } from "@mui/material";
import { CameraAlt as CameraAltIcon } from "@mui/icons-material";

export default function CameraButton({
  config,
  deviceOnline,
  onCameraSettings,
  cameraEnabled,
  getCameraColor,
}) {
  const isCameraAvailable = config?.camera?.installed !== false;
  const isDisabled = !deviceOnline || !config || !isCameraAvailable;

  const handleClick = () => {
    if (!isDisabled && onCameraSettings) {
      onCameraSettings();
    }
  };

  return (
    <Tooltip
      title={
        !config
          ? "Loading configuration..."
          : !isCameraAvailable
          ? "Camera not installed"
          : cameraEnabled
          ? "Camera Hardware Enabled - Click to Disable"
          : "Camera Hardware Disabled - Click to Enable"
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
            backgroundColor: isDisabled ? "action.disabled" : "action.selected",
          },
        }}
      >
        <CameraAltIcon />
      </Fab>
    </Tooltip>
  );
}
