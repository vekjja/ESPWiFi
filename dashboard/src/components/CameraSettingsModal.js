import React, { useState, useEffect, useRef } from "react";
import {
  TextField,
  Box,
  Slider,
  Typography,
  Switch,
  FormControlLabel,
  Accordion,
  AccordionSummary,
  AccordionDetails,
  MenuItem,
  Select,
  FormControl,
  InputLabel,
} from "@mui/material";
import ExpandMoreIcon from "@mui/icons-material/ExpandMore";
import SettingsModal from "./SettingsModal";
import SaveButton from "./SaveButton";

export default function CameraSettingsModal({
  open,
  onClose,
  onSave,
  cameraData,
  onCameraDataChange,
  config,
  saveConfigToDevice,
}) {
  const [localData, setLocalData] = useState(cameraData);
  const isInitialLoad = useRef(true);
  const [cameraSettings, setCameraSettings] = useState({
    frameRate: 10,
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

  useEffect(() => {
    setLocalData(cameraData);
    isInitialLoad.current = true;
  }, [cameraData]);

  // Notify parent component when localData changes (but not on initial load)
  useEffect(() => {
    if (onCameraDataChange && !isInitialLoad.current) {
      onCameraDataChange(localData);
    }
    isInitialLoad.current = false;
  }, [localData, onCameraDataChange]);

  // Load camera settings when modal opens
  useEffect(() => {
    if (open) {
      loadCameraSettings();
    }
  }, [open, config]);

  const loadCameraSettings = () => {
    // Load camera settings from config
    if (config?.camera) {
      setCameraSettings({
        frameRate: config.camera.frameRate ?? 10,
        brightness: config.camera.brightness ?? 1,
        contrast: config.camera.contrast ?? 1,
        saturation: config.camera.saturation ?? 1,
        exposure_level: config.camera.exposure_level ?? 1,
        exposure_value: config.camera.exposure_value ?? 400,
        agc_gain: config.camera.agc_gain ?? 2,
        gain_ceiling: config.camera.gain_ceiling ?? 2,
        white_balance: config.camera.white_balance ?? 1,
        awb_gain: config.camera.awb_gain ?? 1,
        wb_mode: config.camera.wb_mode ?? 0,
      });

      // Also update rotation in localData if it's in config
      setLocalData((prevLocalData) => {
        const newLocalData = { ...prevLocalData };
        if (config.camera.rotation !== undefined) {
          newLocalData.rotation = config.camera.rotation;
        }
        return newLocalData;
      });
    }
  };

  const handleSave = () => {
    // Save camera settings to global config
    if (config && saveConfigToDevice) {
      const updatedConfig = {
        ...config,
        camera: {
          ...config.camera,
          ...cameraSettings,
          rotation: localData.rotation,
        },
      };
      saveConfigToDevice(updatedConfig);
    }

    if (onSave) {
      onSave();
    }
  };

  const handleDataChange = (field, value) => {
    const newData = { ...localData, [field]: value };
    setLocalData(newData);
  };

  const handleCameraSettingChange = (field, value) => {
    setCameraSettings((prev) => ({ ...prev, [field]: value }));
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title="Camera Settings"
      actions={<SaveButton onClick={handleSave} tooltip="Add Camera Module" />}
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
          </AccordionSummary>
          <AccordionDetails>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 3 }}>
              {/* Frame Rate */}
              <Box>
                <Typography gutterBottom>Frame Rate (FPS)</Typography>
                <Slider
                  value={cameraSettings.frameRate}
                  onChange={(e, value) =>
                    handleCameraSettingChange("frameRate", value)
                  }
                  min={1}
                  max={60}
                  step={1}
                  marks={[
                    { value: 1, label: "1" },
                    { value: 10, label: "10" },
                    { value: 30, label: "30" },
                    { value: 60, label: "60" },
                  ]}
                  valueLabelDisplay="auto"
                />
              </Box>

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
