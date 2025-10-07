import React, { useState, useEffect } from "react";
import { Container, Fab, Tooltip } from "@mui/material";
import CameraAltIcon from "@mui/icons-material/CameraAlt";

export default function CameraSettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
}) {
  // Camera settings state
  const [enabled, setEnabled] = useState(false);

  useEffect(() => {
    if (config?.camera) {
      setEnabled(config.camera.enabled || false);
    }
  }, [config]);

  // Get camera button color based on state
  const getCameraColor = () => {
    if (!enabled) return "text.disabled";
    return "success.main"; // Green when enabled
  };

  const handleToggleCamera = () => {
    const newEnabledState = !enabled;
    setEnabled(newEnabledState);

    const configToSave = {
      ...config,
      camera: {
        ...config.camera,
        enabled: newEnabledState,
        frameRate: config.camera?.frameRate || 10,
      },
    };

    // Save to device immediately
    saveConfigToDevice(configToSave);
  };

  return (
    <Container
      sx={{
        display: "flex",
        flexWrap: "wrap",
        justifyContent: "center",
      }}
    >
      <Tooltip
        title={
          enabled
            ? "Camera Hardware Enabled - Click to Disable"
            : "Camera Hardware Disabled - Click to Enable"
        }
      >
        <Fab
          size="small"
          color="primary"
          aria-label="camera-toggle"
          onClick={handleToggleCamera}
          sx={{
            position: "fixed",
            top: "20px",
            left: "80px", // Position next to network settings button
            color: getCameraColor(),
            backgroundColor: enabled ? "action.hover" : "action.disabled",
            "&:hover": {
              backgroundColor: enabled
                ? "action.selected"
                : "action.disabledBackground",
            },
          }}
        >
          <CameraAltIcon />
        </Fab>
      </Tooltip>
    </Container>
  );
}
