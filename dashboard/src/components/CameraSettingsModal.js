import React, { useState, useEffect } from "react";
import {
  TextField,
  Box,
  useTheme,
  Slider,
  Typography,
  Switch,
  FormControlLabel,
  Accordion,
  AccordionSummary,
  AccordionDetails,
  Chip,
  MenuItem,
  Select,
  FormControl,
  InputLabel,
} from "@mui/material";
import ExpandMoreIcon from "@mui/icons-material/ExpandMore";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";
import { getSaveIcon } from "../utils/themeUtils";

export default function CameraSettingsModal({
  open,
  onClose,
  onSave,
  cameraData,
  onCameraDataChange,
  config,
}) {
  const theme = useTheme();
  const SaveIcon = getSaveIcon(theme);
  const [localData, setLocalData] = useState(cameraData);
  const [cameraSettings, setCameraSettings] = useState({
    brightness: 1,
    contrast: 1,
    saturation: 1,
    exposure_level: 1,
    exposure_value: 400,
    agc_gain: 2,
    gain_ceiling: 2,
    white_balance: 1,
    awb_gain: 1,
    wb_mode: 0,
  });
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    setLocalData(cameraData);
  }, [cameraData]);

  // Load camera settings when modal opens
  useEffect(() => {
    if (open) {
      loadCameraSettings();
    }
  }, [open]);

  const loadCameraSettings = async () => {
    try {
      setLoading(true);
      const apiURL = config?.apiURL || "";
      const response = await fetch(`${apiURL}/api/camera/settings`);
      if (response.ok) {
        const settings = await response.json();
        setCameraSettings(settings);

        // Also update frame rate and rotation in localData if they're in the response
        if (
          settings.frameRate !== undefined ||
          settings.rotation !== undefined
        ) {
          const newLocalData = { ...localData };
          if (settings.frameRate !== undefined) {
            newLocalData.frameRate = settings.frameRate;
          }
          if (settings.rotation !== undefined) {
            newLocalData.rotation = settings.rotation;
          }
          setLocalData(newLocalData);
          if (onCameraDataChange) {
            onCameraDataChange(newLocalData);
          }
        }
      }
    } catch (error) {
      console.error("Failed to load camera settings:", error);
    } finally {
      setLoading(false);
    }
  };

  const saveCameraSettings = async () => {
    try {
      setLoading(true);
      const apiURL = config?.apiURL || "";
      const settingsToSave = {
        ...cameraSettings,
        frameRate: localData.frameRate || 10,
        rotation: localData.rotation || 0,
      };

      const response = await fetch(`${apiURL}/api/camera/settings`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify(settingsToSave),
      });

      if (response.ok) {
        console.log("Camera settings saved successfully");
      } else {
        console.error("Failed to save camera settings");
      }
    } catch (error) {
      console.error("Failed to save camera settings:", error);
    } finally {
      setLoading(false);
    }
  };

  const handleSave = () => {
    saveCameraSettings();
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

  const handleCameraSettingChange = (field, value) => {
    setCameraSettings((prev) => ({ ...prev, [field]: value }));
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
          placeholder="Enter camera URL (e.g., /camera or ws://192.168.1.100:8080/camera)"
          fullWidth
          variant="outlined"
          helperText="Local: /camera | Remote: ws://hostname:port/camera | HTTP: http://hostname:port/camera"
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

        <FormControl fullWidth>
          <InputLabel>Rotation</InputLabel>
          <Select
            value={localData.rotation || 0}
            label="Rotation"
            onChange={(e) =>
              handleDataChange("rotation", parseInt(e.target.value))
            }
          >
            <MenuItem value={0}>0° (No rotation)</MenuItem>
            <MenuItem value={90}>90° (Clockwise)</MenuItem>
            <MenuItem value={180}>180° (Upside down)</MenuItem>
            <MenuItem value={270}>270° (Counter-clockwise)</MenuItem>
          </Select>
        </FormControl>

        {/* Camera Sensor Settings */}
        <Accordion defaultExpanded>
          <AccordionSummary expandIcon={<ExpandMoreIcon />}>
            <Typography variant="h6">Camera Sensor Settings</Typography>
            <Chip
              label={loading ? "Loading..." : "Live"}
              color={loading ? "default" : "success"}
              size="small"
              sx={{ ml: 2 }}
            />
          </AccordionSummary>
          <AccordionDetails>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 3 }}>
              {/* Brightness */}
              <Box>
                <Typography gutterBottom>Brightness</Typography>
                <Slider
                  value={cameraSettings.brightness}
                  onChange={(e, value) =>
                    handleCameraSettingChange("brightness", value)
                  }
                  min={-2}
                  max={2}
                  step={1}
                  marks
                  valueLabelDisplay="auto"
                  disabled={loading}
                />
              </Box>

              {/* Contrast */}
              <Box>
                <Typography gutterBottom>Contrast</Typography>
                <Slider
                  value={cameraSettings.contrast}
                  onChange={(e, value) =>
                    handleCameraSettingChange("contrast", value)
                  }
                  min={-2}
                  max={2}
                  step={1}
                  marks
                  valueLabelDisplay="auto"
                  disabled={loading}
                />
              </Box>

              {/* Saturation */}
              <Box>
                <Typography gutterBottom>Saturation</Typography>
                <Slider
                  value={cameraSettings.saturation}
                  onChange={(e, value) =>
                    handleCameraSettingChange("saturation", value)
                  }
                  min={-2}
                  max={2}
                  step={1}
                  marks
                  valueLabelDisplay="auto"
                  disabled={loading}
                />
              </Box>

              {/* Exposure Level */}
              <Box>
                <Typography gutterBottom>Exposure Level</Typography>
                <Slider
                  value={cameraSettings.exposure_level}
                  onChange={(e, value) =>
                    handleCameraSettingChange("exposure_level", value)
                  }
                  min={-2}
                  max={2}
                  step={1}
                  marks
                  valueLabelDisplay="auto"
                  disabled={loading}
                />
              </Box>

              {/* Exposure Value */}
              <Box>
                <Typography gutterBottom>Exposure Value</Typography>
                <Slider
                  value={cameraSettings.exposure_value}
                  onChange={(e, value) =>
                    handleCameraSettingChange("exposure_value", value)
                  }
                  min={0}
                  max={1200}
                  step={50}
                  marks={[
                    { value: 0, label: "0" },
                    { value: 300, label: "300" },
                    { value: 600, label: "600" },
                    { value: 900, label: "900" },
                    { value: 1200, label: "1200" },
                  ]}
                  valueLabelDisplay="auto"
                  disabled={loading}
                />
              </Box>

              {/* AGC Gain */}
              <Box>
                <Typography gutterBottom>AGC Gain</Typography>
                <Slider
                  value={cameraSettings.agc_gain}
                  onChange={(e, value) =>
                    handleCameraSettingChange("agc_gain", value)
                  }
                  min={0}
                  max={30}
                  step={1}
                  marks={[
                    { value: 0, label: "0" },
                    { value: 10, label: "10" },
                    { value: 20, label: "20" },
                    { value: 30, label: "30" },
                  ]}
                  valueLabelDisplay="auto"
                  disabled={loading}
                />
              </Box>

              {/* Gain Ceiling */}
              <Box>
                <Typography gutterBottom>Gain Ceiling</Typography>
                <Slider
                  value={cameraSettings.gain_ceiling}
                  onChange={(e, value) =>
                    handleCameraSettingChange("gain_ceiling", value)
                  }
                  min={0}
                  max={6}
                  step={1}
                  marks
                  valueLabelDisplay="auto"
                  disabled={loading}
                />
              </Box>

              {/* White Balance Controls */}
              <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
                <FormControlLabel
                  control={
                    <Switch
                      checked={cameraSettings.white_balance === 1}
                      onChange={(e) =>
                        handleCameraSettingChange(
                          "white_balance",
                          e.target.checked ? 1 : 0
                        )
                      }
                      disabled={loading}
                    />
                  }
                  label="Auto White Balance"
                />

                <FormControlLabel
                  control={
                    <Switch
                      checked={cameraSettings.awb_gain === 1}
                      onChange={(e) =>
                        handleCameraSettingChange(
                          "awb_gain",
                          e.target.checked ? 1 : 0
                        )
                      }
                      disabled={loading}
                    />
                  }
                  label="AWB Gain"
                />

                <Box>
                  <Typography gutterBottom>White Balance Mode</Typography>
                  <Slider
                    value={cameraSettings.wb_mode}
                    onChange={(e, value) =>
                      handleCameraSettingChange("wb_mode", value)
                    }
                    min={0}
                    max={4}
                    step={1}
                    marks={[
                      { value: 0, label: "Auto" },
                      { value: 1, label: "Sunny" },
                      { value: 2, label: "Cloudy" },
                      { value: 3, label: "Office" },
                      { value: 4, label: "Home" },
                    ]}
                    valueLabelDisplay="auto"
                    disabled={loading}
                  />
                </Box>
              </Box>
            </Box>
          </AccordionDetails>
        </Accordion>
      </Box>
    </SettingsModal>
  );
}
