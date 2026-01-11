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
  Accordion,
  AccordionSummary,
  AccordionDetails,
  Divider,
  Chip,
} from "@mui/material";
import ExpandMoreIcon from "@mui/icons-material/ExpandMore";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import BrightnessIcon from "@mui/icons-material/Brightness6";
import ContrastIcon from "@mui/icons-material/Contrast";
import TimerIcon from "@mui/icons-material/Timer";
import ExposureIcon from "@mui/icons-material/WbSunny";
import RotateIcon from "@mui/icons-material/Rotate90DegreesCcw";
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

  const [expandedPanels, setExpandedPanels] = useState({
    basic: true,
    image: false,
    exposure: false,
    whiteBalance: false,
  });

  const handleAccordionChange = (panel) => (event, isExpanded) => {
    setExpandedPanels((prev) => ({ ...prev, [panel]: isExpanded }));
  };

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

  // Only initialize once when modal opens, don't sync while modal is open
  useEffect(() => {
    if (open && !initializedRef.current) {
      loadCameraSettings();
      initializedRef.current = true;
    } else if (!open) {
      // Reset the ref when modal closes so it can initialize again next time
      initializedRef.current = false;
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  // Update camera settings when config changes (while modal is open)
  useEffect(() => {
    if (open && initializedRef.current) {
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
        sharpness: config.camera.sharpness ?? 0,
        denoise: config.camera.denoise ?? 0,
        quality: config.camera.quality ?? 12,
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

  const handleSave = async () => {
    console.log("CameraSettingsModal handleSave");
    console.log("Current camera settings:", cameraSettings);
    console.log("localData:", localData);

    if (!config || !saveConfigToDevice) {
      console.warn("No config or saveConfigToDevice function");
      return;
    }

    setLoading(true);

    try {
      const updatedConfig = {
        ...config,
        camera: {
          ...config.camera,
          frameRate: cameraSettings.frameRate,
          rotation: cameraSettings.rotation,
          brightness: cameraSettings.brightness,
          contrast: cameraSettings.contrast,
          saturation: cameraSettings.saturation,
          sharpness: cameraSettings.sharpness,
          denoise: cameraSettings.denoise,
          quality: cameraSettings.quality,
          exposure_level: cameraSettings.exposure_level,
          exposure_value: cameraSettings.exposure_value,
          agc_gain: cameraSettings.agc_gain,
          gain_ceiling: cameraSettings.gain_ceiling,
          white_balance: cameraSettings.white_balance,
          awb_gain: cameraSettings.awb_gain,
          wb_mode: cameraSettings.wb_mode,
        },
      };

      console.log("Saving camera config to device:", updatedConfig.camera);
      await saveConfigToDevice(updatedConfig);
      console.log("Camera settings saved successfully");

      // Call onSave callback if provided (for module-level name update)
      if (onSave && !isDeviceLevel) {
        console.log("Calling onSave callback with localData:", localData);
        onSave(localData);
      }

      onClose();
    } catch (err) {
      console.error("Error saving camera settings:", err);
    } finally {
      setLoading(false);
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
    console.log("CameraSettingsModal handleDelete called");
    if (onDelete) {
      console.log("Calling onDelete callback");
      onDelete();
      onClose();
    } else {
      console.warn("No onDelete callback provided");
    }
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 1,
          }}
        >
          <CameraAltIcon sx={{ color: "primary.main" }} />
          Camera
        </Box>
      }
      actions={
        <>
          {onDelete && !isDeviceLevel && (
            <DeleteButton onClick={handleDelete} tooltip="Delete Camera" />
          )}
          <SaveButton
            onClick={handleSave}
            tooltip={
              loading
                ? "Saving..."
                : isDeviceLevel
                ? "Save camera settings"
                : "Save Camera Module"
            }
            disabled={loading}
          />
        </>
      }
      maxWidth="md"
    >
      <Box>
        <Stack spacing={2}>
          {/* Module-level: Camera Name only (no URL configuration) */}
          {!isDeviceLevel && (
            <Card elevation={1} sx={{ bgcolor: "background.paper" }}>
              <CardContent>
                <TextField
                  label="Camera Name"
                  value={localData?.name || ""}
                  onChange={(e) => handleDataChange("name", e.target.value)}
                  placeholder="Enter camera name"
                  fullWidth
                  variant="outlined"
                  helperText="Camera always streams over /ws/control (LAN or cloud tunnel)"
                />
              </CardContent>
            </Card>
          )}

          {/* Camera Sensor Settings - Only for device-level */}
          {isDeviceLevel && (
            <Box>
              {/* Basic Settings */}
              <Accordion
                expanded={expandedPanels.basic}
                onChange={handleAccordionChange("basic")}
                sx={{
                  bgcolor: "background.paper",
                  "&:before": { display: "none" },
                }}
              >
                <AccordionSummary expandIcon={<ExpandMoreIcon />}>
                  <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                    <TimerIcon color="primary" />
                    <Typography variant="h6">Basic Settings</Typography>
                  </Box>
                </AccordionSummary>
                <AccordionDetails>
                  <Stack spacing={3}>
                    {/* Frame Rate */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Frame Rate
                        </Typography>
                        <Chip
                          label={`${cameraSettings.frameRate} FPS`}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
                      <Slider
                        value={cameraSettings.frameRate}
                        onChange={(e, value) =>
                          handleCameraSettingChange("frameRate", value)
                        }
                        min={1}
                        max={30}
                        step={1}
                        marks={[
                          { value: 1, label: "1" },
                          { value: 10, label: "10" },
                          { value: 20, label: "20" },
                          { value: 30, label: "30" },
                        ]}
                        valueLabelDisplay="auto"
                        disabled={loading}
                      />
                      <Typography variant="caption" color="text.secondary">
                        Higher frame rates provide smoother video but use more
                        bandwidth
                      </Typography>
                    </Box>

                    <Divider />

                    {/* Rotation */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Box
                          sx={{ display: "flex", alignItems: "center", gap: 1 }}
                        >
                          <RotateIcon color="primary" fontSize="small" />
                          <Typography variant="body1" fontWeight="medium">
                            Rotation
                          </Typography>
                        </Box>
                        <Chip
                          label={`${cameraSettings.rotation}°`}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
                      <Slider
                        value={cameraSettings.rotation}
                        onChange={(e, value) =>
                          handleCameraSettingChange("rotation", value)
                        }
                        min={0}
                        max={270}
                        step={90}
                        marks={[
                          { value: 0, label: "0°" },
                          { value: 90, label: "90°" },
                          { value: 180, label: "180°" },
                          { value: 270, label: "270°" },
                        ]}
                        valueLabelDisplay="auto"
                        disabled={loading}
                      />
                      <Typography variant="caption" color="text.secondary">
                        Rotate the camera image by 90° increments
                      </Typography>
                    </Box>
                  </Stack>
                </AccordionDetails>
              </Accordion>

              {/* Image Quality Settings */}
              <Accordion
                expanded={expandedPanels.image}
                onChange={handleAccordionChange("image")}
                sx={{
                  bgcolor: "background.paper",
                  "&:before": { display: "none" },
                }}
              >
                <AccordionSummary expandIcon={<ExpandMoreIcon />}>
                  <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                    <ContrastIcon color="primary" />
                    <Typography variant="h6">Image Quality</Typography>
                  </Box>
                </AccordionSummary>
                <AccordionDetails>
                  <Stack spacing={3}>
                    {/* Brightness */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Box
                          sx={{ display: "flex", alignItems: "center", gap: 1 }}
                        >
                          <BrightnessIcon color="primary" fontSize="small" />
                          <Typography variant="body1" fontWeight="medium">
                            Brightness
                          </Typography>
                        </Box>
                        <Chip
                          label={cameraSettings.brightness}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Contrast
                        </Typography>
                        <Chip
                          label={cameraSettings.contrast}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Saturation
                        </Typography>
                        <Chip
                          label={cameraSettings.saturation}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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

                    <Divider />

                    {/* Sharpness */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Sharpness
                        </Typography>
                        <Chip
                          label={cameraSettings.sharpness}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
                      <Slider
                        value={cameraSettings.sharpness}
                        onChange={(e, value) =>
                          handleCameraSettingChange("sharpness", value)
                        }
                        min={-2}
                        max={2}
                        step={1}
                        marks
                        valueLabelDisplay="auto"
                        disabled={loading}
                      />
                      <Typography variant="caption" color="text.secondary">
                        Edge enhancement (-2 = soft, +2 = sharp)
                      </Typography>
                    </Box>

                    <Divider />

                    {/* Denoise */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Denoise
                        </Typography>
                        <Chip
                          label={cameraSettings.denoise}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
                      <Slider
                        value={cameraSettings.denoise}
                        onChange={(e, value) =>
                          handleCameraSettingChange("denoise", value)
                        }
                        min={0}
                        max={8}
                        step={1}
                        marks
                        valueLabelDisplay="auto"
                        disabled={loading}
                      />
                      <Typography variant="caption" color="text.secondary">
                        Noise reduction (0 = off, 8 = maximum)
                      </Typography>
                    </Box>

                    <Divider />

                    {/* Quality */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          JPEG Quality
                        </Typography>
                        <Chip
                          label={cameraSettings.quality}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
                      <Slider
                        value={cameraSettings.quality}
                        onChange={(e, value) =>
                          handleCameraSettingChange("quality", value)
                        }
                        min={0}
                        max={63}
                        step={1}
                        marks={[
                          { value: 0, label: "Best" },
                          { value: 12, label: "Default" },
                          { value: 63, label: "Fast" },
                        ]}
                        valueLabelDisplay="auto"
                        disabled={loading}
                      />
                      <Typography variant="caption" color="text.secondary">
                        Lower values = better quality, higher file size
                      </Typography>
                    </Box>
                  </Stack>
                </AccordionDetails>
              </Accordion>

              {/* Exposure Settings */}
              <Accordion
                expanded={expandedPanels.exposure}
                onChange={handleAccordionChange("exposure")}
                sx={{
                  bgcolor: "background.paper",
                  "&:before": { display: "none" },
                }}
              >
                <AccordionSummary expandIcon={<ExpandMoreIcon />}>
                  <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                    <ExposureIcon color="primary" />
                    <Typography variant="h6">Exposure & Gain</Typography>
                  </Box>
                </AccordionSummary>
                <AccordionDetails>
                  <Stack spacing={3}>
                    {/* Exposure Level */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Exposure Level
                        </Typography>
                        <Chip
                          label={cameraSettings.exposure_level}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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
                      <Typography variant="caption" color="text.secondary">
                        Adjust automatic exposure compensation
                      </Typography>
                    </Box>

                    {/* Exposure Value */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Exposure Value
                        </Typography>
                        <Chip
                          label={cameraSettings.exposure_value}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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
                          { value: 400, label: "400" },
                          { value: 800, label: "800" },
                          { value: 1200, label: "1200" },
                        ]}
                        valueLabelDisplay="auto"
                        disabled={loading}
                      />
                      <Typography variant="caption" color="text.secondary">
                        Manual exposure time control (0 = auto)
                      </Typography>
                    </Box>

                    <Divider />

                    {/* AGC Gain */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          AGC Gain
                        </Typography>
                        <Chip
                          label={cameraSettings.agc_gain}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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
                      <Typography variant="caption" color="text.secondary">
                        Automatic gain control (0 = auto)
                      </Typography>
                    </Box>

                    {/* Gain Ceiling */}
                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          Gain Ceiling
                        </Typography>
                        <Chip
                          label={`${cameraSettings.gain_ceiling}x`}
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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
                      <Typography variant="caption" color="text.secondary">
                        Maximum gain amplification
                      </Typography>
                    </Box>
                  </Stack>
                </AccordionDetails>
              </Accordion>

              {/* White Balance Settings */}
              <Accordion
                expanded={expandedPanels.whiteBalance}
                onChange={handleAccordionChange("whiteBalance")}
                sx={{
                  bgcolor: "background.paper",
                  "&:before": { display: "none" },
                }}
              >
                <AccordionSummary expandIcon={<ExpandMoreIcon />}>
                  <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                    <CameraAltIcon color="primary" />
                    <Typography variant="h6">White Balance</Typography>
                  </Box>
                </AccordionSummary>
                <AccordionDetails>
                  <Stack spacing={3}>
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
                      label={
                        <Box>
                          <Typography variant="body1" fontWeight="medium">
                            Auto White Balance
                          </Typography>
                          <Typography variant="caption" color="text.secondary">
                            Automatically adjust color temperature
                          </Typography>
                        </Box>
                      }
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
                      label={
                        <Box>
                          <Typography variant="body1" fontWeight="medium">
                            AWB Gain
                          </Typography>
                          <Typography variant="caption" color="text.secondary">
                            Enable automatic white balance gain
                          </Typography>
                        </Box>
                      }
                    />

                    <Divider />

                    <Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                          alignItems: "center",
                          mb: 1,
                        }}
                      >
                        <Typography variant="body1" fontWeight="medium">
                          White Balance Mode
                        </Typography>
                        <Chip
                          label={
                            ["Auto", "Sunny", "Cloudy", "Office", "Home"][
                              cameraSettings.wb_mode
                            ]
                          }
                          size="small"
                          color="primary"
                          variant="outlined"
                        />
                      </Box>
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
                      <Typography variant="caption" color="text.secondary">
                        Preset white balance for different lighting conditions
                      </Typography>
                    </Box>
                  </Stack>
                </AccordionDetails>
              </Accordion>
            </Box>
          )}
        </Stack>
      </Box>
    </SettingsModal>
  );
}
