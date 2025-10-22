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
  const handleClick = () => {
    if (deviceOnline && onCameraSettings) {
      onCameraSettings();
    }
  };

  return (
    <Tooltip
      title={
        cameraEnabled
          ? "Camera Hardware Enabled - Click to Disable"
          : "Camera Hardware Disabled - Click to Enable"
      }
    >
      <Fab
        size="medium"
        color="primary"
        onClick={handleClick}
        disabled={!deviceOnline}
        sx={{
          color: !deviceOnline
            ? "text.disabled"
            : getCameraColor
            ? getCameraColor()
            : "primary.main",
          backgroundColor: !deviceOnline ? "action.disabled" : "action.hover",
          "&:hover": {
            backgroundColor: !deviceOnline
              ? "action.disabled"
              : "action.selected",
          },
        }}
      >
        <CameraAltIcon />
      </Fab>
    </Tooltip>
  );
}
