import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import {
  Box,
  Typography,
  IconButton,
  Tooltip,
  TextField,
  Slider,
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
import {
  PlayArrow,
  Pause,
  Fullscreen,
  FullscreenExit,
  CameraAlt,
  ExpandMore,
} from "@mui/icons-material";
import SaveIcon from "@mui/icons-material/SaveAs";
import SettingsModal from "./SettingsModal";
import IButton from "./IButton";
import DeleteButton from "./DeleteButton";

export default function CameraModule({
  config,
  globalConfig,
  onUpdate,
  onDelete,
  deviceOnline = true,
}) {
  const [isStreaming, setIsStreaming] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [streamUrl, setStreamUrl] = useState("");
  const [imageUrl, setImageUrl] = useState("");
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [cameraStatus, setCameraStatus] = useState("unknown"); // "enabled", "disabled", or "unknown"
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "",
    url: config?.url || "/camera",
    frameRate: config?.frameRate || 10,
    rotation: globalConfig?.camera?.rotation || 0,
  });
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
  const wsRef = useRef(null);
  const imgRef = useRef(null);
  const imageUrlRef = useRef("");

  // Function to check if camera URL is for a remote device
  const isRemoteCamera = () => {
    const cameraUrl = config?.url || "/camera";
    // If URL starts with http://, https://, or contains a hostname (not just a path)
    return (
      cameraUrl.startsWith("http://") ||
      cameraUrl.startsWith("https://") ||
      (!cameraUrl.startsWith("/") && cameraUrl.includes("."))
    );
  };

  // Function to get the remote device's config endpoint URL
  const getRemoteConfigUrl = () => {
    const cameraUrl = config?.url || "/camera";

    if (cameraUrl.startsWith("ws://")) {
      // Convert ws://hostname:port/path to http://hostname:port/config
      const url = new URL(cameraUrl);
      return `${url.protocol.replace("ws", "http")}//${url.host}/config`;
    } else if (cameraUrl.startsWith("wss://")) {
      // Convert wss://hostname:port/path to https://hostname:port/config
      const url = new URL(cameraUrl);
      return `${url.protocol.replace("wss", "https")}//${url.host}/config`;
    } else if (
      cameraUrl.startsWith("http://") ||
      cameraUrl.startsWith("https://")
    ) {
      // Extract hostname and port from HTTP URL
      const url = new URL(cameraUrl);
      return `${url.protocol}//${url.host}/config`;
    } else if (!cameraUrl.startsWith("/") && cameraUrl.includes(".")) {
      // Handle hostname without protocol
      return `http://${cameraUrl.split("/")[0]}/config`;
    }

    return null;
  };

  // Function to poll remote camera status
  const pollRemoteCameraStatus = async () => {
    const remoteConfigUrl = getRemoteConfigUrl();
    if (!remoteConfigUrl) return;

    try {
      const response = await fetch(remoteConfigUrl, {
        method: "GET",
        headers: {
          Accept: "application/json",
        },
        signal: AbortSignal.timeout(3000), // 3 second timeout
      });

      if (response.ok) {
        const data = await response.json();
        setCameraStatus(data.camera?.enabled ? "enabled" : "disabled");
      } else {
        setCameraStatus("unknown");
      }
    } catch (error) {
      console.error("Error polling remote camera status:", error);
      setCameraStatus("unknown");
    }
  };

  // Function to update camera status based on global config and device online status
  const updateCameraStatus = () => {
    if (isRemoteCamera()) {
      // For remote cameras, poll their status
      pollRemoteCameraStatus();
    } else {
      // For local cameras, use global config
      if (!deviceOnline) {
        setCameraStatus("unknown");
      } else if (globalConfig?.camera?.enabled) {
        setCameraStatus("enabled");
      } else {
        setCameraStatus("disabled");
      }
    }
  };

  useEffect(() => {
    // Convert relative path to absolute URL like WebSocketModule does
    let wsUrl = config?.url || "/camera";

    // Check if URL already has a protocol
    if (!wsUrl.startsWith("ws://") && !wsUrl.startsWith("wss://")) {
      if (wsUrl.startsWith("/")) {
        // For relative paths, use the mDNS hostname from global config
        const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
        const mdnsHostname = globalConfig?.mdns;
        const hostname = mdnsHostname
          ? `${mdnsHostname}.local`
          : window.location.hostname;
        const port =
          window.location.port && !mdnsHostname
            ? `:${window.location.port}`
            : "";
        wsUrl = `${protocol}//${hostname}${port}${wsUrl}`;
      } else {
        // URL doesn't have protocol and doesn't start with /, add ws:// protocol
        wsUrl = `ws://${wsUrl}`;
      }
    }

    setStreamUrl(wsUrl);
  }, [config?.url, globalConfig?.mdns]); // Re-run if URL or mDNS changes

  // Update camera status when global config or device online status changes
  useEffect(() => {
    updateCameraStatus();
  }, [globalConfig?.camera?.enabled, deviceOnline]); // Re-run when camera enabled status or device online status changes

  // Set up polling for remote cameras
  useEffect(() => {
    if (!isRemoteCamera()) return;

    // Initial status check
    updateCameraStatus();

    // Set up polling every 10 seconds for remote cameras (frequent but smart)
    const pollInterval = setInterval(() => {
      updateCameraStatus();
    }, 10000);

    // Cleanup interval on unmount
    return () => {
      clearInterval(pollInterval);
    };
  }, [config?.url]); // Re-run when camera URL changes

  const handleStartStream = () => {
    if (!streamUrl) {
      console.error("No stream URL available");
      return;
    }

    // Close existing connection if any
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }

    // Validate URL format
    try {
      new URL(streamUrl);
    } catch (error) {
      console.error(`Invalid WebSocket URL: ${streamUrl}`);
      return;
    }

    try {
      const ws = new WebSocket(streamUrl);
      ws.binaryType = "arraybuffer";

      wsRef.current = ws;
      setIsStreaming(false); // Set to connecting state

      ws.onopen = () => {
        setIsStreaming(true);
      };

      ws.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
          const blob = new Blob([event.data], { type: "image/jpeg" });
          const objectURL = URL.createObjectURL(blob);

          // Clean up previous URL to prevent memory leaks
          if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
            URL.revokeObjectURL(imageUrlRef.current);
          }

          imageUrlRef.current = objectURL;
          setImageUrl(objectURL);
        }
      };

      ws.onerror = (error) => {
        console.error("❌ Camera WebSocket error:", error);
        setIsStreaming(false);
      };

      ws.onclose = (event) => {
        setIsStreaming(false);
        wsRef.current = null;
      };
    } catch (error) {
      console.error("❌ Failed to create camera WebSocket:", error);
    }
  };

  const handleStopStream = () => {
    if (wsRef.current) {
      wsRef.current.close();
      wsRef.current = null;
    }
    setIsStreaming(false);

    // Clean up image URL
    if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
      URL.revokeObjectURL(imageUrlRef.current);
      imageUrlRef.current = "";
      setImageUrl("");
    }
  };

  const handleFullscreen = () => {
    if (!isFullscreen) {
      // Enter fullscreen
      if (imgRef.current && imgRef.current.requestFullscreen) {
        imgRef.current.requestFullscreen();
        setIsFullscreen(true);
      }
    } else {
      // Exit fullscreen
      if (document.exitFullscreen) {
        document.exitFullscreen();
        setIsFullscreen(false);
      }
    }
  };

  const handleSnapshot = () => {
    // Get the base URL from the current window location
    const protocol = window.location.protocol;
    const hostname = window.location.hostname;
    const port = window.location.port ? `:${window.location.port}` : "";
    const snapshotUrl = `${protocol}//${hostname}${port}/camera/snapshot`;

    // Open the snapshot in a new tab
    window.open(snapshotUrl, "_blank");
  };

  const handleOpenSettings = () => {
    setSettingsData({
      name: config?.name || "",
      url: config?.url || "/camera",
      frameRate: config?.frameRate || 10,
      rotation: globalConfig?.camera?.rotation || 0,
    });
    setSettingsModalOpen(true);
    loadCameraSettings();
  };

  const loadCameraSettings = async () => {
    try {
      setLoading(true);
      const apiURL = globalConfig?.apiURL || "";
      const response = await fetch(`${apiURL}/api/camera/settings`);
      if (response.ok) {
        const settings = await response.json();
        setCameraSettings(settings);

        // Update rotation in settingsData if present in response
        if (settings.rotation !== undefined) {
          setSettingsData((prev) => ({ ...prev, rotation: settings.rotation }));
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
      const apiURL = globalConfig?.apiURL || "";
      const response = await fetch(`${apiURL}/api/camera/settings`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify(cameraSettings),
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

  const handleCameraSettingChange = (field, value) => {
    setCameraSettings((prev) => ({ ...prev, [field]: value }));
  };

  const handleCloseSettings = () => {
    setSettingsModalOpen(false);
  };

  const handleSaveSettings = () => {
    const updatedModule = {
      ...config,
      name: settingsData.name,
      url: settingsData.url,
      frameRate: settingsData.frameRate,
    };
    onUpdate(config?.key, updatedModule);

    // Save camera settings to API
    saveCameraSettings();
    handleCloseSettings();
  };

  const handleDeleteModule = () => {
    onDelete(config?.key);
    handleCloseSettings();
  };

  // Listen for fullscreen changes
  useEffect(() => {
    const handleFullscreenChange = () => {
      setIsFullscreen(!!document.fullscreenElement);
    };

    document.addEventListener("fullscreenchange", handleFullscreenChange);
    return () => {
      document.removeEventListener("fullscreenchange", handleFullscreenChange);
    };
  }, []);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      // Clean up object URL
      if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
        URL.revokeObjectURL(imageUrlRef.current);
      }

      // Close connection
      handleStopStream();
    };
  }, []);

  return (
    <>
      <Module
        title={config?.name || "Camera"}
        onSettings={handleOpenSettings}
        sx={{
          minWidth: "300px",
          maxWidth: "400px",
          minHeight: "auto",
          maxHeight: "400px",
          "& .MuiCardContent-root": {
            minHeight: "auto",
            paddingBottom: "0px !important",
          },
        }}
      >
        <Box
          sx={{
            width: "100%",
            height: "160px",
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            backgroundColor: "rgba(0, 0, 0, 0.3)",
            borderRadius: 1,
            position: "relative",
            overflow: "hidden",
          }}
        >
          {isStreaming && imageUrl ? (
            <img
              ref={imgRef}
              src={imageUrl}
              alt="Camera Stream"
              style={{
                width: "100%",
                height: "100%",
                objectFit: "contain",
                borderRadius: "4px",
                transform: `rotate(${globalConfig?.camera?.rotation || 0}deg)`,
              }}
            />
          ) : (
            <Box
              sx={{
                display: "flex",
                flexDirection: "column",
                alignItems: "center",
                justifyContent: "center",
                color: "text.secondary",
              }}
            >
              <Typography variant="h6" gutterBottom>
                📷 Camera {isRemoteCamera() && "🌐 "}
                {isStreaming
                  ? "Live"
                  : cameraStatus === "enabled"
                  ? "Ready"
                  : cameraStatus === "disabled"
                  ? "Disabled"
                  : "Offline"}
              </Typography>
              <Typography variant="body2">
                {isStreaming
                  ? "Streaming in progress"
                  : cameraStatus === "enabled"
                  ? "Click play to start streaming"
                  : cameraStatus === "disabled"
                  ? isRemoteCamera()
                    ? "Camera disabled on remote device"
                    : "Enable from the device control panel"
                  : isRemoteCamera()
                  ? "Remote camera is not available"
                  : "Camera is not available"}
              </Typography>
            </Box>
          )}

          {/* Stream status indicator */}
          <Box
            sx={{
              position: "absolute",
              bottom: 8,
              left: 8,
              display: "flex",
              alignItems: "center",
              gap: 1,
              backgroundColor: "rgba(0, 0, 0, 0.7)",
              borderRadius: 1,
              padding: "4px 8px",
            }}
          >
            <Box
              sx={{
                width: 8,
                height: 8,
                borderRadius: "50%",
                backgroundColor: isStreaming
                  ? "success.main"
                  : cameraStatus === "unknown"
                  ? "error.main"
                  : cameraStatus === "disabled"
                  ? "warning.main"
                  : "primary.main",
              }}
            />
            <Typography variant="caption" sx={{ color: "white" }}>
              {isStreaming
                ? "Live"
                : cameraStatus === "unknown"
                ? isRemoteCamera()
                  ? "Remote Offline"
                  : "Offline"
                : cameraStatus === "disabled"
                ? isRemoteCamera()
                  ? "Remote Disabled"
                  : "Disabled"
                : isRemoteCamera()
                ? "Remote Online"
                : "Online"}
            </Typography>
          </Box>
        </Box>

        {/* Camera controls at the bottom */}
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 2,
            padding: 0.5,
            backgroundColor: "rgba(0, 0, 0, 0.1)",
            borderRadius: 1,
            marginTop: 0.5,
          }}
        >
          <Tooltip title={isStreaming ? "Stop Stream" : "Start Stream"}>
            <span>
              <IconButton
                onClick={isStreaming ? handleStopStream : handleStartStream}
                disabled={cameraStatus !== "enabled"}
                sx={{
                  color:
                    cameraStatus === "enabled"
                      ? "primary.main"
                      : "text.disabled",
                }}
              >
                {isStreaming ? <Pause /> : <PlayArrow />}
              </IconButton>
            </span>
          </Tooltip>

          <Tooltip title={isFullscreen ? "Exit Fullscreen" : "Fullscreen"}>
            <IconButton
              onClick={handleFullscreen}
              sx={{ color: "primary.main" }}
            >
              {isFullscreen ? <FullscreenExit /> : <Fullscreen />}
            </IconButton>
          </Tooltip>

          <Tooltip title="Take Snapshot">
            <IconButton onClick={handleSnapshot} sx={{ color: "primary.main" }}>
              <CameraAlt />
            </IconButton>
          </Tooltip>
        </Box>
      </Module>

      <SettingsModal
        open={settingsModalOpen}
        onClose={handleCloseSettings}
        title="Camera Module Settings"
        actions={
          <>
            <DeleteButton
              onClick={handleDeleteModule}
              tooltip="Delete Camera Module"
            />
            <IButton
              color="primary"
              Icon={SaveIcon}
              onClick={handleSaveSettings}
              tooltip="Save Camera Settings"
            />
          </>
        }
      >
        <Box sx={{ marginTop: 2 }}>
          <TextField
            fullWidth
            label="Camera Name"
            value={settingsData.name}
            onChange={(e) =>
              setSettingsData({ ...settingsData, name: e.target.value })
            }
            sx={{ marginBottom: 3 }}
          />

          <TextField
            fullWidth
            label="Camera URL"
            value={settingsData.url}
            onChange={(e) =>
              setSettingsData({ ...settingsData, url: e.target.value })
            }
            placeholder="/camera"
            helperText="WebSocket endpoint for camera stream"
            sx={{ marginBottom: 3 }}
          />

          <Typography gutterBottom>
            Frame Rate: {settingsData.frameRate} FPS
          </Typography>
          <Slider
            value={settingsData.frameRate}
            onChange={(e, newValue) =>
              setSettingsData({ ...settingsData, frameRate: newValue })
            }
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

          <FormControl fullWidth sx={{ marginTop: 3 }}>
            <InputLabel>Rotation</InputLabel>
            <Select
              value={settingsData.rotation || 0}
              label="Rotation"
              onChange={(e) =>
                setSettingsData({
                  ...settingsData,
                  rotation: parseInt(e.target.value),
                })
              }
            >
              <MenuItem value={0}>0° (No rotation)</MenuItem>
              <MenuItem value={90}>90° (Clockwise)</MenuItem>
              <MenuItem value={180}>180° (Upside down)</MenuItem>
              <MenuItem value={270}>270° (Counter-clockwise)</MenuItem>
            </Select>
          </FormControl>

          {/* Camera Sensor Settings */}
          <Accordion defaultExpanded sx={{ marginTop: 3 }}>
            <AccordionSummary expandIcon={<ExpandMore />}>
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
    </>
  );
}
