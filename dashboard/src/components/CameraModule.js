import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import { Box, Typography, IconButton, Tooltip } from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import FullscreenIcon from "@mui/icons-material/Fullscreen";
import FullscreenExitIcon from "@mui/icons-material/FullscreenExit";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import CameraSettingsModal from "./CameraSettingsModal";
import { buildWebSocketUrl } from "../utils/apiUtils";

export default function CameraModule({
  config,
  globalConfig,
  onUpdate,
  onDelete,
  deviceOnline = true,
  saveConfigToDevice,
  usingCloudTunnel = false,
  cloudTunnelWsRef = null,
  registerCloudBinaryHandler = null,
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
  const cloudUnsubRef = useRef(null);
  const pendingSnapshotRef = useRef(false);
  const imgRef = useRef(null);
  const imageUrlRef = useRef("");
  const isMountedRef = useRef(true);

  // Update settings data when config changes
  useEffect(() => {
    setSettingsData({
      name: config?.name || "",
    });
  }, [config?.name]);

  const handleStartStream = () => {
    // Cloud tunnel: use control tunnel WebSocket
    if (usingCloudTunnel && cloudTunnelWsRef?.current) {
      const ws = cloudTunnelWsRef.current;
      if (ws.readyState !== WebSocket.OPEN) {
        console.error("Cloud tunnel not connected");
        return;
      }

      // Register binary handler for cloud camera frames
      if (
        typeof registerCloudBinaryHandler === "function" &&
        !cloudUnsubRef.current
      ) {
        cloudUnsubRef.current = registerCloudBinaryHandler((ab) => {
          if (!(ab instanceof ArrayBuffer)) return;

          // Handle snapshot
          if (pendingSnapshotRef.current) {
            pendingSnapshotRef.current = false;
            const blob = new Blob([ab], { type: "image/jpeg" });
            const objectUrl = URL.createObjectURL(blob);
            window.open(objectUrl, "_blank");
            setTimeout(() => URL.revokeObjectURL(objectUrl), 15000);
            return;
          }

          // Handle streaming
          const blob = new Blob([ab], { type: "image/jpeg" });
          const objectURL = URL.createObjectURL(blob);
          if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
            URL.revokeObjectURL(imageUrlRef.current);
          }
          imageUrlRef.current = objectURL;
          setImageUrl(objectURL);
        });
      }

      // Subscribe via control socket
      try {
        ws.send(JSON.stringify({ cmd: "camera_subscribe", enable: true }));
      } catch (err) {
        console.error("Failed to subscribe to cloud camera:", err);
        return;
      }
      setIsStreaming(true);
      return;
    }

    // LAN: connect to /ws/camera
    const mdnsHostname = globalConfig?.hostname || globalConfig?.deviceName;
    const cameraWsUrl = buildWebSocketUrl("/ws/camera", mdnsHostname || null);

    if (!cameraWsUrl) {
      console.error("Failed to build camera WebSocket URL");
      return;
    }

    try {
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
      };

      ws.onclose = () => {
        console.log("📷 Camera WebSocket closed");
        setIsStreaming(false);
        wsRef.current = null;
      };
    } catch (error) {
      console.error("📷 Failed to create camera WebSocket:", error);
    }
  };

  const handleStopStream = () => {
    // Cloud tunnel: unsubscribe
    if (usingCloudTunnel && cloudTunnelWsRef?.current) {
      const ws = cloudTunnelWsRef.current;
      if (ws.readyState === WebSocket.OPEN) {
        try {
          ws.send(JSON.stringify({ cmd: "camera_subscribe", enable: false }));
        } catch {
          // ignore
        }
      }

      // Unregister binary handler
      if (cloudUnsubRef.current) {
        try {
          cloudUnsubRef.current();
        } catch {
          // ignore
        }
        cloudUnsubRef.current = null;
      }

      setIsStreaming(false);

      // Clean up image URL
      if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
        URL.revokeObjectURL(imageUrlRef.current);
        imageUrlRef.current = "";
        setImageUrl("");
      }
      return;
    }

    // LAN: close WebSocket
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

  const handleSnapshot = async () => {
    // Cloud tunnel: request snapshot via control socket
    if (usingCloudTunnel && cloudTunnelWsRef?.current) {
      const ws = cloudTunnelWsRef.current;
      if (ws.readyState === WebSocket.OPEN) {
        // Register handler if not already registered
        if (
          !cloudUnsubRef.current &&
          typeof registerCloudBinaryHandler === "function"
        ) {
          cloudUnsubRef.current = registerCloudBinaryHandler((ab) => {
            if (!(ab instanceof ArrayBuffer)) return;
            if (!pendingSnapshotRef.current) return;
            pendingSnapshotRef.current = false;
            const blob = new Blob([ab], { type: "image/jpeg" });
            const objectUrl = URL.createObjectURL(blob);
            window.open(objectUrl, "_blank");
            setTimeout(() => URL.revokeObjectURL(objectUrl), 15000);
          });
        }
        pendingSnapshotRef.current = true;
        try {
          ws.send(JSON.stringify({ cmd: "camera_snapshot" }));
        } catch (err) {
          console.error("Failed to request snapshot:", err);
        }
      }
      return;
    }

    // LAN: snapshot not supported on /ws/camera (binary only)
    console.warn("Snapshot not supported for LAN streaming yet");
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

  const handleOpenSettings = () => {
    // Set settings data (no URL configuration)
    setSettingsData({
      name: config?.name || "",
    });

    setSettingsModalOpen(true);
  };

  const handleCloseSettings = () => {
    setSettingsModalOpen(false);
  };

  const handleSaveSettings = () => {
    // Update the module config with the new name only
    if (onUpdate && moduleKey) {
      console.log(`📷 Saving camera settings - Name: ${settingsData.name}`);
      onUpdate(moduleKey, {
        name: settingsData.name,
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
        settingsDisabled={false}
        settingsTooltip={"Settings"}
        errorOutline={false}
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
