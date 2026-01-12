import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import { Box, Typography, IconButton, Tooltip } from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import FullscreenIcon from "@mui/icons-material/Fullscreen";
import FullscreenExitIcon from "@mui/icons-material/FullscreenExit";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import CameraSettingsModal from "./CameraSettingsModal";
import { buildWebSocketUrl, buildApiUrl } from "../utils/apiUtils";

export default function CameraModule({
  config,
  globalConfig,
  onUpdate,
  onDelete,
  deviceOnline = true,
  saveConfigToDevice,
  controlWs,
}) {
  const moduleKey = config?.key;
  const [isStreaming, setIsStreaming] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [imageUrl, setImageUrl] = useState("");
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "",
  });

  const wsRef = useRef(null);
  const imgRef = useRef(null);
  const imageUrlRef = useRef("");
  const isMountedRef = useRef(true);
  const intentionalCloseRef = useRef(false);

  // Update settings data when config changes
  useEffect(() => {
    setSettingsData({
      name: config?.name || "",
    });
  }, [config?.name]);

  const handleStartStream = () => {
    // LAN: connect to /ws/camera
    const mdnsHostname = globalConfig?.hostname || globalConfig?.deviceName;
    const cameraWsUrl = buildWebSocketUrl("/ws/camera", mdnsHostname || null);

    if (!cameraWsUrl) {
      console.error("Failed to build camera WebSocket URL");
      return;
    }

    try {
      intentionalCloseRef.current = false; // Reset flag when starting stream
      const ws = new WebSocket(cameraWsUrl);
      ws.binaryType = "arraybuffer";
      wsRef.current = ws;

      ws.onopen = () => {
        console.log("📷 Camera WebSocket connected:", cameraWsUrl);
        setIsStreaming(true);
      };

      ws.onmessage = (event) => {
        if (event.data instanceof ArrayBuffer) {
          const blob = new Blob([event.data], { type: "image/jpeg" });
          const objectURL = URL.createObjectURL(blob);
          if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
            URL.revokeObjectURL(imageUrlRef.current);
          }
          imageUrlRef.current = objectURL;
          setImageUrl(objectURL);
        }
      };

      ws.onerror = (error) => {
        console.error("📷 Camera WebSocket error:", error);
        setIsStreaming(false);
        // Auto-reconnect on error after a delay (if not intentionally closed)
        setTimeout(() => {
          if (
            isMountedRef.current &&
            wsRef.current === ws &&
            !intentionalCloseRef.current
          ) {
            console.log("📷 Attempting to reconnect camera after error...");
            handleStartStream();
          }
        }, 2000);
      };

      ws.onclose = (event) => {
        console.log("📷 Camera WebSocket closed", event.code);
        setIsStreaming(false);
        wsRef.current = null;

        // Auto-reconnect if closed unexpectedly (not a normal close and not intentional)
        if (
          event.code !== 1000 &&
          isMountedRef.current &&
          !intentionalCloseRef.current
        ) {
          console.log("📷 Unexpected close, attempting to reconnect in 2s...");
          setTimeout(() => {
            if (isMountedRef.current && !intentionalCloseRef.current) {
              handleStartStream();
            }
          }, 2000);
        }
      };
    } catch (error) {
      console.error("📷 Failed to create camera WebSocket:", error);
    }
  };

  const handleStopStream = () => {
    // Mark as intentional close to prevent auto-reconnect
    intentionalCloseRef.current = true;

    // Close WebSocket
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

  const handleDeleteModule = () => {
    console.log("🗑️ Camera module delete initiated", { moduleKey });
    // Best-effort cleanup before removing the module
    handleStopStream();
    // Close the settings modal first
    handleCloseSettings();
    // Then delete the module
    if (onDelete && moduleKey !== null && moduleKey !== undefined) {
      console.log("🗑️ Calling onDelete with moduleKey:", moduleKey);
      onDelete(moduleKey);
    } else {
      console.warn("🗑️ Cannot delete: onDelete or moduleKey missing", {
        hasOnDelete: !!onDelete,
        moduleKey,
      });
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
    try {
      // Use control WebSocket if available
      if (controlWs && controlWs.readyState === WebSocket.OPEN) {
        // Send command via WebSocket - always save to get a direct file URL
        const message = JSON.stringify({ cmd: "take_snapshot", save: true });

        // Wait for response
        const handleMessage = (event) => {
          try {
            const response = JSON.parse(event.data);
            if (response.cmd === "take_snapshot") {
              controlWs.removeEventListener("message", handleMessage);

              if (response.ok !== false && response.url) {
                // Open the saved snapshot file URL in a new tab
                const mdnsHostname =
                  globalConfig?.hostname || globalConfig?.deviceName;
                const snapshotUrl = buildApiUrl(response.url, mdnsHostname);
                window.open(snapshotUrl, "_blank");
                console.log("📸 Snapshot saved and opened:", snapshotUrl);
              } else {
                console.error(
                  "📸 Snapshot failed:",
                  response.error || "Unknown error"
                );
              }
            }
          } catch (err) {
            console.error("📸 Error parsing snapshot response:", err);
          }
        };

        controlWs.addEventListener("message", handleMessage);
        controlWs.send(message);

        // Timeout after 5 seconds
        setTimeout(() => {
          controlWs.removeEventListener("message", handleMessage);
        }, 5000);
      } else {
        // Fallback to direct HTTP snapshot endpoint
        const mdnsHostname = globalConfig?.hostname || globalConfig?.deviceName;
        const snapshotUrl = buildApiUrl("/camera/snapshot", mdnsHostname);
        window.open(snapshotUrl, "_blank");
        console.log("📸 Direct snapshot URL:", snapshotUrl);
      }
    } catch (err) {
      console.error("📸 Snapshot error:", err);
    }
  };

  const handleOpenSettings = () => {
    if (!globalConfig || !saveConfigToDevice) {
      console.warn("Cannot open camera settings - config not loaded yet");
      return;
    }
    const data = {
      name: config?.name || "",
    };
    console.log("📷 Opening camera settings with data:", data);
    console.log("📷 moduleKey:", moduleKey);
    console.log("📷 hasOnDelete:", !!onDelete);
    setSettingsData(data);

    setSettingsModalOpen(true);
  };

  const handleCloseSettings = () => {
    setSettingsModalOpen(false);
  };

  const handleSaveSettings = (updatedData) => {
    if (onUpdate && moduleKey && updatedData) {
      onUpdate(moduleKey, {
        name: updatedData.name,
      });
    }
    handleCloseSettings();
  };

  // Listen for fullscreen changes
  useEffect(() => {
    const handleFullscreenChange = () => {
      const isFullscreenActive =
        document.fullscreenElement ||
        document.webkitFullscreenElement ||
        document.mozFullScreenElement ||
        document.msFullscreenElement;

      setIsFullscreen(!!isFullscreenActive);
    };

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
      isMountedRef.current = false;
      intentionalCloseRef.current = true; // Mark as intentional on unmount

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
        settingsDisabled={false}
        settingsTooltip={"Settings"}
        errorOutline={false}
        data-camera-module="true"
        sx={{
          minWidth: "400px",
          maxWidth: "500px",
          minHeight: "auto",
          maxHeight: "500px",
          "& .MuiCardContent-root": {
            minHeight: "auto",
            paddingBottom: "0px !important",
          },
        }}
      >
        <Box
          sx={{
            width: "100%",
            height: "280px",
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
                📷 Camera {isStreaming ? "Live" : "Ready"}
              </Typography>
              <Typography variant="body2">
                {isStreaming ? "Streaming in progress" : "Click play to start"}
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
                backgroundColor: isStreaming ? "success.main" : "primary.main",
              }}
            />
            <Typography variant="caption" sx={{ color: "white" }}>
              {isStreaming ? "Live" : "Idle"}
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
                sx={{
                  color: "primary.main",
                }}
              >
                {isStreaming ? <PauseIcon /> : <PlayArrowIcon />}
              </IconButton>
            </span>
          </Tooltip>

          <Tooltip title={"Take Snapshot"}>
            <span>
              <IconButton
                onClick={handleSnapshot}
                sx={{
                  color: "primary.main",
                }}
              >
                <CameraAltIcon />
              </IconButton>
            </span>
          </Tooltip>

          <Tooltip title={isFullscreen ? "Exit Fullscreen" : "Fullscreen"}>
            <span>
              <IconButton
                onClick={handleFullscreen}
                sx={{
                  color: "primary.main",
                }}
              >
                {isFullscreen ? <FullscreenExitIcon /> : <FullscreenIcon />}
              </IconButton>
            </span>
          </Tooltip>
        </Box>
      </Module>

      <CameraSettingsModal
        open={settingsModalOpen && globalConfig && saveConfigToDevice}
        onClose={handleCloseSettings}
        onSave={handleSaveSettings}
        onDelete={handleDeleteModule}
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
