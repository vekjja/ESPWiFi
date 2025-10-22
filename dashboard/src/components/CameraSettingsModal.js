import React, { useState, useEffect } from "react";
import { TextField, Box, useTheme } from "@mui/material";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";
import { getSaveIcon } from "../utils/themeUtils";

export default function CameraSettingsModal({
  open,
  onClose,
  onSave,
  cameraData,
  onCameraDataChange,
}) {
  const theme = useTheme();
  const SaveIcon = getSaveIcon(theme);
  const [localData, setLocalData] = useState(cameraData);

  useEffect(() => {
    setLocalData(cameraData);
  }, [cameraData]);

  const handleSave = () => {
    if (onSave) {
      onSave();
    }
  };

  const handleDataChange = (field, value) => {
    const newData = { ...localData, [field]: value };
    setLocalData(newData);
    if (onCameraDataChange) {
      onCameraDataChange(newData);
    }
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title="Camera Settings"
      actions={
        <IButton
          color="primary"
          Icon={SaveIcon}
          onClick={handleSave}
          tooltip="Add Camera Module"
        />
      }
    >
      <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
        <TextField
          label="Camera Name"
          value={localData.name || ""}
          onChange={(e) => handleDataChange("name", e.target.value)}
          placeholder="Enter camera name"
          fullWidth
          variant="outlined"
        />

        <TextField
          label="Camera URL"
          value={localData.url || ""}
          onChange={(e) => handleDataChange("url", e.target.value)}
          placeholder="Enter camera URL (e.g., /camera)"
          fullWidth
          variant="outlined"
          helperText="Use relative path (e.g., /camera) for same server, or full URL for external cameras"
        />

        <TextField
          label="Frame Rate (FPS)"
          type="number"
          value={localData.frameRate || 10}
          onChange={(e) =>
            handleDataChange("frameRate", parseInt(e.target.value) || 10)
          }
          inputProps={{ min: 1, max: 60 }}
          fullWidth
          variant="outlined"
          helperText="Frames per second (1-60)"
        />
      </Box>
    </SettingsModal>
  );
}
