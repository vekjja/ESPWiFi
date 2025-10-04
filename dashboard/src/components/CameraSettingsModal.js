import React, { useState, useEffect, useRef } from "react";
import {
  Container,
  Fab,
  Tooltip,
  FormControl,
  FormControlLabel,
  Switch,
  Slider,
  Typography,
  Box,
} from "@mui/material";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import SaveIcon from "@mui/icons-material/SaveAs";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";

export default function CameraSettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
}) {
  const [isModalOpen, setIsModalOpen] = useState(false);

  // Camera settings state
  const [enabled, setEnabled] = useState(false);
  const [frameRate, setFrameRate] = useState(10);

  // Camera status state
  const [isStreaming, setIsStreaming] = useState(false);
  const [configSaved, setConfigSaved] = useState(false);
  const wsRef = useRef(null);

  useEffect(() => {
    if (config?.camera) {
      setEnabled(config.camera.enabled || false);
      setFrameRate(config.camera.frameRate || 10);
    }
  }, [config]);

  // WebSocket connection to monitor camera status
  useEffect(() => {
    if (enabled && configSaved) {
      // Add a delay to allow the backend to start the camera service
      const connectTimeout = setTimeout(() => {
        // Construct WebSocket URL
        let wsUrl = "/camera";
        if (wsUrl.startsWith("/")) {
          const protocol =
            window.location.protocol === "https:" ? "wss:" : "ws:";
          const hostname = window.location.hostname;
          const port = window.location.port ? `:${window.location.port}` : "";
          wsUrl = `${protocol}//${hostname}${port}${wsUrl}`;
        }

        // Connect to Camera WebSocket to monitor status
        const ws = new WebSocket(wsUrl);
        wsRef.current = ws;

        ws.onopen = () => {
          setIsStreaming(true);
        };

        ws.onmessage = (event) => {
          // Camera is sending data, so it's streaming
          setIsStreaming(true);
        };

        ws.onerror = (error) => {
          console.error("Camera WebSocket error:", error);
          setIsStreaming(false);
        };

        ws.onclose = () => {
          setIsStreaming(false);
          wsRef.current = null;
        };
      }, 1000); // 1 second delay

      return () => {
        clearTimeout(connectTimeout);
        if (wsRef.current) {
          wsRef.current.close();
          wsRef.current = null;
        }
      };
    } else {
      // Disconnect if disabled or config not saved
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
      setIsStreaming(false);
    }
  }, [enabled, configSaved]);

  const handleOpenModal = () => {
    setIsModalOpen(true);
  };

  const handleCloseModal = () => {
    setIsModalOpen(false);
  };

  const handleEnabledChange = (event) => {
    setEnabled(event.target.checked);
  };

  const handleFrameRateChange = (event, newValue) => {
    setFrameRate(newValue);
  };

  // Get camera button color based on state
  const getCameraColor = () => {
    if (!enabled) return "text.disabled";
    if (!configSaved) return "primary.main";
    if (isStreaming) return "success.main";
    return "warning.main";
  };

  const handleSave = () => {
    const configToSave = {
      ...config,
      camera: {
        enabled: enabled,
        frameRate: frameRate,
      },
    };

    // Save to device (not just local config)
    // The backend will automatically start/stop camera based on enabled state
    saveConfigToDevice(configToSave);

    // Mark config as saved so WebSocket can connect
    setConfigSaved(true);

    handleCloseModal();
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
            ? configSaved
              ? `Camera Settings - ${
                  isStreaming ? "Streaming" : "Connected, not streaming"
                }`
              : "Camera Settings - Save config to start"
            : "Camera Settings - Disabled"
        }
      >
        <Fab
          size="small"
          color="primary"
          aria-label="camera-settings"
          onClick={handleOpenModal}
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

      <SettingsModal
        open={isModalOpen}
        onClose={handleCloseModal}
        title="Camera Settings"
        actions={
          <IButton
            color="primary"
            Icon={SaveIcon}
            onClick={handleSave}
            tooltip={"Save Camera Settings to Device"}
          />
        }
      >
        <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
          <FormControlLabel
            control={
              <Switch
                checked={enabled}
                onChange={handleEnabledChange}
                color="primary"
              />
            }
            label="Enable Camera"
          />
        </FormControl>

        {enabled && (
          <Box sx={{ marginTop: 3 }}>
            <Typography gutterBottom>Frame Rate: {frameRate} FPS</Typography>
            <Slider
              value={frameRate}
              onChange={handleFrameRateChange}
              min={1}
              max={30}
              step={1}
              marks={[
                { value: 1, label: "1" },
                { value: 5, label: "5" },
                { value: 10, label: "10" },
                { value: 15, label: "15" },
                { value: 20, label: "20" },
                { value: 25, label: "25" },
                { value: 30, label: "30" },
              ]}
              valueLabelDisplay="auto"
              sx={{
                color: "primary.main",
                "& .MuiSlider-thumb": {
                  backgroundColor: "primary.main",
                },
                "& .MuiSlider-track": {
                  backgroundColor: "primary.main",
                },
                "& .MuiSlider-rail": {
                  backgroundColor: "rgba(255, 255, 255, 0.2)",
                },
                "& .MuiSlider-mark": {
                  backgroundColor: "primary.main",
                },
                "& .MuiSlider-markLabel": {
                  color: "primary.main",
                },
              }}
            />
          </Box>
        )}

        {enabled && (
          <Box
            sx={{
              marginTop: 2,
              padding: 2,
              backgroundColor: "rgba(71, 255, 240, 0.1)",
              borderRadius: 1,
            }}
          >
            <Typography variant="body2" color="primary.main">
              📷 Camera will be started when settings are saved. A camera module
              will be added to the dashboard for live streaming.
            </Typography>
            <Typography
              variant="caption"
              sx={{ marginTop: 1, color: "primary.main", display: "block" }}
            >
              WebSocket URL: ws://{window.location.hostname}:
              {window.location.port || 80}/camera
            </Typography>
          </Box>
        )}
      </SettingsModal>
    </Container>
  );
}
