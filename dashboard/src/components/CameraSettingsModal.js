import React, { useState, useEffect, useRef } from "react";
import {
  TextField,
  Box,
  Slider,
  Typography,
  Switch,
  FormControlLabel,
  Card,
  CardContent,
  Stack,
  Tooltip,
} from "@mui/material";
import { CameraAlt as CameraAltIcon } from "@mui/icons-material";
import NoPhotographyIcon from "@mui/icons-material/NoPhotography";
import SettingsModal from "./SettingsModal";
import SaveButton from "./SaveButton";
import DeleteButton from "./DeleteButton";

export default function CameraSettingsModal({
  open,
  onClose,
  onSave,
  onDelete,
  cameraData,
  onCameraDataChange,
  config,
  saveConfigToDevice,
  moduleConfig,
  onModuleUpdate,
  deviceOnline,
}) {
  // Determine if this is device-level (no cameraData/moduleConfig) or module-level
  const isDeviceLevel = !cameraData && !moduleConfig;

  const [localData, setLocalData] = useState(cameraData);
  const isInitialLoad = useRef(true);
  const [loading, setLoading] = useState(false);
  const [enabled, setEnabled] = useState(config?.camera?.enabled || false);
  const initializedRef = useRef(false);

  const [cameraSettings, setCameraSettings] = useState({
    frameRate: 10,
    rotation: 0,
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

  // Update enabled state when config changes (but only if modal is already initialized)
  useEffect(() => {
    if (
      initializedRef.current &&
      config?.camera?.enabled !== undefined &&
      isDeviceLevel
    ) {
      setEnabled(config.camera.enabled);
    }
  }, [config?.camera?.enabled, isDeviceLevel]);

  // Only initialize once when modal opens, don't sync while modal is open
  useEffect(() => {
    if (open && !initializedRef.current) {
      if (isDeviceLevel) {
        setEnabled(config?.camera?.enabled || false);
      }
      loadCameraSettings();
      initializedRef.current = true;
    } else if (!open) {
      // Reset the ref when modal closes so it can initialize again next time
      initializedRef.current = false;
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  // Load camera settings when modal opens
  useEffect(() => {
    if (open) {
      loadCameraSettings();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, config, moduleConfig]);

  const loadCameraSettings = () => {
    // Load camera settings from config
    // Rotation is per-module, other settings are global
    if (config?.camera) {
      setCameraSettings({
        frameRate: config.camera.frameRate ?? 10,
        rotation: moduleConfig?.rotation ?? config.camera.rotation ?? 0,
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
    }
  };

  const handleToggleEnabled = async () => {
    if (
      loading ||
      !deviceOnline ||
      !config ||
      !saveConfigToDevice ||
      !isDeviceLevel
    ) {
      return;
    }

    const newEnabled = !enabled;
    const previousEnabled = enabled;
    setEnabled(newEnabled);
    setLoading(true);

    // If disabling camera, disconnect any active WebSocket connections
    if (!newEnabled) {
      // Find and disconnect any active camera WebSocket connections
      const cameraModules = document.querySelectorAll("[data-camera-module]");
      cameraModules.forEach((module) => {
        // Trigger WebSocket disconnection in camera modules
        const event = new CustomEvent("cameraDisable");
        module.dispatchEvent(event);
      });

      // Also disconnect any global WebSocket connections
      if (window.cameraWebSockets) {
        window.cameraWebSockets.forEach((ws) => {
          if (ws && ws.readyState === WebSocket.OPEN) {
            ws.close();
          }
        });
        window.cameraWebSockets = [];
      }
    }

    try {
      const configToSave = {
        ...config,
        camera: {
          ...config.camera,
          enabled: newEnabled,
          frameRate: config.camera?.frameRate || 10,
        },
      };

      // Save to device immediately
      saveConfigToDevice(configToSave);
    } catch (err) {
      console.error("Error toggling Camera:", err);
      // Revert on error
      setEnabled(previousEnabled);
    } finally {
      setLoading(false);
    }
  };

  const handleSave = async () => {
    // Save camera settings
    // Rotation is per-module, other settings are global
    if (config && saveConfigToDevice) {
      setLoading(true);

      try {
        const updatedConfig = {
          ...config,
          camera: {
            ...config.camera,
            // Save global camera settings (excluding rotation)
            frameRate: cameraSettings.frameRate,
            brightness: cameraSettings.brightness,
            contrast: cameraSettings.contrast,
            saturation: cameraSettings.saturation,
            exposure_level: cameraSettings.exposure_level,
            exposure_value: cameraSettings.exposure_value,
            agc_gain: cameraSettings.agc_gain,
            gain_ceiling: cameraSettings.gain_ceiling,
            white_balance: cameraSettings.white_balance,
            awb_gain: cameraSettings.awb_gain,
            wb_mode: cameraSettings.wb_mode,
          },
        };

        // Save rotation to module config if module update function is provided
        if (onModuleUpdate && moduleConfig?.key) {
          onModuleUpdate(moduleConfig.key, {
            rotation: cameraSettings.rotation,
          });
        }

        await saveConfigToDevice(updatedConfig);
      } catch (err) {
        console.error("Error saving camera settings:", err);
      } finally {
        setLoading(false);
      }
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

  const handleDelete = () => {
    if (onDelete) {
      onDelete();
      onClose();
    }
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <Tooltip
          title={
            isDeviceLevel
              ? enabled
                ? "Click to disable Camera"
                : "Click to enable Camera"
              : ""
          }
        >
          <Box
            onClick={isDeviceLevel ? handleToggleEnabled : undefined}
            sx={{
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              gap: 1,
              cursor: isDeviceLevel ? "pointer" : "default",
              "&:hover": isDeviceLevel
                ? {
                    opacity: 0.8,
                  }
                : {},
            }}
          >
            {isDeviceLevel && !enabled ? (
              <NoPhotographyIcon
                sx={{
                  color: "text.disabled",
                }}
              />
            ) : (
              <CameraAltIcon
                sx={{
                  color:
                    isDeviceLevel && enabled ? "primary.main" : "primary.main",
                }}
              />
            )}
            Camera
          </Box>
        </Tooltip>
      }
      actions={
        isDeviceLevel ? null : (
          <>
            {onDelete && (
              <DeleteButton onClick={handleDelete} tooltip="Delete Camera" />
            )}
            <SaveButton onClick={handleSave} tooltip="Add Camera Module" />
          </>
        )
      }
      maxWidth="md"
    >
      <Box>
        <Stack spacing={3}>
          {/* Module-level: Camera Name and URL */}
          {!isDeviceLevel && (
            <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
              <TextField
                label="Camera Name"
                value={localData?.name || ""}
                onChange={(e) => handleDataChange("name", e.target.value)}
                placeholder="Enter camera name"
                fullWidth
                variant="outlined"
              />

              <TextField
                label="Camera URL"
                value={localData?.url || ""}
                onChange={(e) => handleDataChange("url", e.target.value)}
                placeholder="Enter camera URL (e.g., /camera or ws://192.168.1.100:8080/camera)"
                fullWidth
                variant="outlined"
                helperText="Local: /camera | Remote: ws://hostname:port/camera | HTTP: http://hostname:port/camera"
              />
            </Box>
          )}

          {/* Camera Sensor Settings - Only for device-level */}
          {isDeviceLevel && (
            <Card
              elevation={2}
              sx={{
                bgcolor: "background.paper",
              }}
            >
              <CardContent>
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
                      disabled={!enabled}
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
                      disabled={!enabled}
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
                      disabled={!enabled}
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
                      disabled={!enabled}
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
                      disabled={!enabled}
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
                      disabled={!enabled}
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
                      disabled={!enabled}
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
                      disabled={!enabled}
                    />
                  </Box>

                  {/* White Balance Controls */}
                  <Box
                    sx={{
                      display: "flex",
                      flexDirection: "column",
                      gap: 2,
                    }}
                  >
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
                          disabled={!enabled}
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
                          disabled={!enabled}
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
                        disabled={!enabled}
                      />
                    </Box>
                  </Box>
                </Box>

                {/* Save Button for device-level */}
                <Box sx={{ mt: 3, display: "flex", justifyContent: "center" }}>
                  <SaveButton
                    onClick={handleSave}
                    tooltip={
                      loading ? "Saving..." : "Save camera settings to device"
                    }
                    disabled={loading || !deviceOnline || !enabled}
                  />
                </Box>
              </CardContent>
            </Card>
          )}
        </Stack>
      </Box>
    </SettingsModal>
  );
}
