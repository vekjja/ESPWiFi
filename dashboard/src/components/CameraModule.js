import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import { Box, Typography, IconButton, Tooltip } from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import FullscreenIcon from "@mui/icons-material/Fullscreen";
import FullscreenExitIcon from "@mui/icons-material/FullscreenExit";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import CameraSettingsModal from "./CameraSettingsModal";
import {
  buildApiUrl,
  buildWebSocketUrl,
  getFetchOptions,
} from "../utils/apiUtils";

export default function CameraModule({
  config,
  globalConfig,
  onUpdate,
  onDelete,
  deviceOnline = true,
  saveConfigToDevice,
}) {
  const moduleKey = config?.key;
  const [isStreaming, setIsStreaming] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [streamUrl, setStreamUrl] = useState("");
  const [imageUrl, setImageUrl] = useState("");
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [cameraStatus, setCameraStatus] = useState("unknown"); // "enabled", "disabled", or "unknown"
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "",
    url: config?.url || "/ws/camera",
  });

  const wsRef = useRef(null);
  const imgRef = useRef(null);
  const imageUrlRef = useRef("");
  const isMountedRef = useRef(true);

  // Function to check if camera URL is for a remote device
  const isRemoteCamera = () => {
    const cameraUrl = config?.url || "/ws/camera";
    // If URL starts with http://, https://, or contains a hostname (not just a path)
    return (
      cameraUrl.startsWith("http://") ||
      cameraUrl.startsWith("https://") ||
      (!cameraUrl.startsWith("/") && cameraUrl.includes("."))
    );
  };

  // Function to get the remote device's config endpoint URL
  const getRemoteConfigUrl = () => {
    const cameraUrl = config?.url || "/ws/camera";

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
    // Don't poll if component is unmounted
    if (!isMountedRef.current) return;

    const remoteConfigUrl = getRemoteConfigUrl();
    if (!remoteConfigUrl) return;

    try {
      const response = await fetch(
        remoteConfigUrl,
        getFetchOptions({
          method: "GET",
          headers: {
            Accept: "application/json",
          },
          signal: AbortSignal.timeout(3000), // 3 second timeout
        })
      );

      // Check if still mounted before updating state
      if (!isMountedRef.current) return;

      if (response.ok) {
        const data = await response.json();
        setCameraStatus(data.camera?.enabled ? "enabled" : "disabled");
      } else {
        setCameraStatus("unknown");
      }
    } catch (error) {
      // Only log error if component is still mounted
      if (isMountedRef.current) {
        console.error("Error polling remote camera status:", error);
        setCameraStatus("unknown");
      }
    }
  };

  // Function to update camera status based on global config and device online status
  const updateCameraStatus = () => {
    // Don't update if component is unmounted
    if (!isMountedRef.current) return;

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
    // Convert relative path to absolute URL
    let wsUrl = config?.url || "/ws/camera";

    // Check if URL already has a protocol
    if (!wsUrl.startsWith("ws://") && !wsUrl.startsWith("wss://")) {
      if (wsUrl.startsWith("/")) {
        // For relative paths, prefer the device hostname (stable + matches mDNS),
        // and fall back to deviceName only if hostname isn't available.
        const mdnsHostname = globalConfig?.hostname || globalConfig?.deviceName;
        wsUrl = buildWebSocketUrl(wsUrl, mdnsHostname || null);
      } else {
        // URL doesn't have protocol and doesn't start with /, add ws:// protocol
        wsUrl = `ws://${wsUrl}`;
      }
    }

    // If URL changed and we're currently streaming, reconnect
    const urlChanged = streamUrl !== "" && streamUrl !== wsUrl;
    if (urlChanged && isStreaming) {
      console.log(
        `📷 Camera URL changed from ${streamUrl} to ${wsUrl}, reconnecting...`
      );
      handleStopStream();
      setStreamUrl(wsUrl);
      // Give it a moment to close the old connection before starting new one
      setTimeout(() => {
        setStreamUrl(wsUrl);
      }, 100);
    } else {
      setStreamUrl(wsUrl);
    }
  }, [config?.url, globalConfig?.hostname, globalConfig?.deviceName]); // Re-run if URL or device identity changes

  // Update camera status when global config or device online status changes
  useEffect(() => {
    updateCameraStatus();
  }, [globalConfig?.camera?.enabled, deviceOnline]); // Re-run when camera enabled status or device online status changes

  // Listen for camera disable events and disconnect WebSocket
  useEffect(() => {
    const handleCameraDisable = () => {
      console.log(
        "📷 Camera disable event received, disconnecting WebSocket..."
      );
      handleStopStream();
    };

    // Add event listener for camera disable
    const moduleElement = document.querySelector("[data-camera-module]");
    if (moduleElement) {
      moduleElement.addEventListener("cameraDisable", handleCameraDisable);
    }

    // Cleanup event listener
    return () => {
      if (moduleElement) {
        moduleElement.removeEventListener("cameraDisable", handleCameraDisable);
      }
    };
  }, []);

  // Update settings data when config changes
  useEffect(() => {
    setSettingsData({
      name: config?.name || "",
      url: config?.url || "/ws/camera",
    });
  }, [config?.name, config?.url]);

  // Set up polling for remote cameras
  useEffect(() => {
    if (!isRemoteCamera()) return;

    // Initial status check
    updateCameraStatus();

    // Set up polling every 10 seconds for remote cameras (frequent but smart)
    const pollInterval = setInterval(() => {
      // Only poll if component is still mounted
      if (isMountedRef.current) {
        updateCameraStatus();
      }
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

      // Track WebSocket globally for cleanup
      if (!window.cameraWebSockets) {
        window.cameraWebSockets = [];
      }
      window.cameraWebSockets.push(ws);

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

      // Remove from global tracking
      if (window.cameraWebSockets) {
        const index = window.cameraWebSockets.indexOf(wsRef.current);
        if (index > -1) {
          window.cameraWebSockets.splice(index, 1);
        }
      }

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

  const handleDeleteModule = () => {
    // Best-effort cleanup before removing the module
    handleStopStream();
    if (onDelete && moduleKey) {
      onDelete(moduleKey);
    }
  };

  const handleFullscreen = () => {
    if (!isFullscreen) {
      // Enter fullscreen
      const element = imgRef.current || document.documentElement;

      if (element.requestFullscreen) {
        element.requestFullscreen();
      } else if (element.webkitRequestFullscreen) {
        // Safari
        element.webkitRequestFullscreen();
      } else if (element.webkitEnterFullscreen) {
        // iOS Safari
        element.webkitEnterFullscreen();
      } else if (element.mozRequestFullScreen) {
        // Firefox
        element.mozRequestFullScreen();
      } else if (element.msRequestFullscreen) {
        // IE/Edge
        element.msRequestFullscreen();
      }

      setIsFullscreen(true);
    } else {
      // Exit fullscreen
      if (document.exitFullscreen) {
        document.exitFullscreen();
      } else if (document.webkitExitFullscreen) {
        document.webkitExitFullscreen();
      } else if (document.mozCancelFullScreen) {
        document.mozCancelFullScreen();
      } else if (document.msExitFullscreen) {
        document.msExitFullscreen();
      }

      setIsFullscreen(false);
    }
  };

  const handleSnapshot = async () => {
    const mdnsHostname = globalConfig?.deviceName;
    const baseUrl = buildApiUrl("/api/camera/snapshot", mdnsHostname);
    const snapshotUrl = `${baseUrl}?save=true`; // Always save to SD

    try {
      const response = await fetch(
        snapshotUrl,
        getFetchOptions({
          method: "GET",
          headers: {
            Accept: "image/jpeg",
          },
          signal: AbortSignal.timeout(10000),
        })
      );

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      const blob = await response.blob();
      const objectUrl = URL.createObjectURL(blob);

      console.log("📷 Snapshot saved to SD card: /snap");
      window.open(objectUrl, "_blank");

      // Best-effort cleanup after a bit (the new tab will have loaded it by then).
      setTimeout(() => URL.revokeObjectURL(objectUrl), 15000);
    } catch (err) {
      console.error("Failed to fetch snapshot:", err);
      // fallback: attempt direct open (may still work if auth disabled)
      window.open(snapshotUrl, "_blank");
    }
  };

  const handleOpenSettings = () => {
    // Set settings data
    setSettingsData({
      name: config?.name || "",
      url: config?.url || "/ws/camera",
    });

    setSettingsModalOpen(true);
  };

  const handleCloseSettings = () => {
    setSettingsModalOpen(false);
  };

  const handleSaveSettings = () => {
    // Update the module config with the new name and URL
    if (onUpdate && moduleKey) {
      console.log(
        `📷 Saving camera settings - URL: ${settingsData.url}, Name: ${settingsData.name}`
      );
      onUpdate(moduleKey, {
        name: settingsData.name,
        url: settingsData.url,
      });
    }
    handleCloseSettings();
  };

  // Listen for fullscreen changes
  useEffect(() => {
    const handleFullscreenChange = () => {
      // Check for fullscreen across all browser prefixes
      const isFullscreenActive =
        document.fullscreenElement ||
        document.webkitFullscreenElement ||
        document.mozFullScreenElement ||
        document.msFullscreenElement;

      setIsFullscreen(!!isFullscreenActive);
    };

    // Add listeners for all browser prefixes
    document.addEventListener("fullscreenchange", handleFullscreenChange);
    document.addEventListener("webkitfullscreenchange", handleFullscreenChange);
    document.addEventListener("mozfullscreenchange", handleFullscreenChange);
    document.addEventListener("MSFullscreenChange", handleFullscreenChange);

    return () => {
      document.removeEventListener("fullscreenchange", handleFullscreenChange);
      document.removeEventListener(
        "webkitfullscreenchange",
        handleFullscreenChange
      );
      document.removeEventListener(
        "mozfullscreenchange",
        handleFullscreenChange
      );
      document.removeEventListener(
        "MSFullscreenChange",
        handleFullscreenChange
      );
    };
  }, []);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      // Mark component as unmounted to prevent further API calls
      isMountedRef.current = false;

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
        settingsDisabled={!deviceOnline}
        settingsTooltip={!deviceOnline ? "Device is offline" : "Settings"}
        errorOutline={!deviceOnline}
        data-camera-module="true"
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
                transform: config?.rotation
                  ? `rotate(${config.rotation}deg)`
                  : undefined,
                transition: "transform 0.3s ease",
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
                    : "Enable from the device's control panel"
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
          <Tooltip
            title={
              !deviceOnline
                ? "Device is offline"
                : isStreaming
                ? "Stop Stream"
                : "Start Stream"
            }
          >
            <span>
              <IconButton
                onClick={isStreaming ? handleStopStream : handleStartStream}
                disabled={!deviceOnline || cameraStatus !== "enabled"}
                sx={{
                  color:
                    !deviceOnline || cameraStatus !== "enabled"
                      ? "text.disabled"
                      : "primary.main",
                }}
              >
                {isStreaming ? <PauseIcon /> : <PlayArrowIcon />}
              </IconButton>
            </span>
          </Tooltip>

          <Tooltip
            title={!deviceOnline ? "Device is offline" : "Take Snapshot"}
          >
            <span>
              <IconButton
                onClick={handleSnapshot}
                disabled={!deviceOnline}
                sx={{
                  color: !deviceOnline ? "text.disabled" : "primary.main",
                }}
              >
                <CameraAltIcon />
              </IconButton>
            </span>
          </Tooltip>

          <Tooltip
            title={
              !deviceOnline
                ? "Device is offline"
                : isFullscreen
                ? "Exit Fullscreen"
                : "Fullscreen"
            }
          >
            <span>
              <IconButton
                onClick={handleFullscreen}
                disabled={!deviceOnline}
                sx={{
                  color: !deviceOnline ? "text.disabled" : "primary.main",
                }}
              >
                {isFullscreen ? <FullscreenExitIcon /> : <FullscreenIcon />}
              </IconButton>
            </span>
          </Tooltip>
        </Box>
      </Module>

      <CameraSettingsModal
        open={settingsModalOpen}
        onClose={handleCloseSettings}
        onSave={handleSaveSettings}
        onDelete={onDelete ? handleDeleteModule : undefined}
        cameraData={settingsData}
        onCameraDataChange={setSettingsData}
        config={globalConfig}
        saveConfigToDevice={saveConfigToDevice}
        moduleConfig={config}
        onModuleUpdate={onUpdate}
      />
    </>
  );
}
