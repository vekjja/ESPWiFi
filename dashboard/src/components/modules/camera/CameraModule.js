import React, { useState, useEffect, useRef } from "react";
import Module from "../Module";
import { Box, Typography, IconButton, Tooltip } from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import FullscreenIcon from "@mui/icons-material/Fullscreen";
import FullscreenExitIcon from "@mui/icons-material/FullscreenExit";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import CameraSettingsModal from "./CameraSettingsModal";
import { SECTIONS } from "../../common/SectionTitle";
import { buildWebSocketUrl, buildApiUrl } from "../../../utils/api";
import { createSocketSession } from "../../../utils/socketSession";
import { connectionLabel } from "../moduleConfig";

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
  const [wantsStream, setWantsStream] = useState(false);
  const [streamStatus, setStreamStatus] = useState("disconnected");
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [imageUrl, setImageUrl] = useState("");
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "",
  });

  const sessionRef = useRef(null);
  const imgRef = useRef(null);
  const containerRef = useRef(null); // Container for fullscreen with rotation
  const imageUrlRef = useRef("");
  const isMountedRef = useRef(true);
  const hostnameRef = useRef(globalConfig?.hostname || globalConfig?.deviceName);
  hostnameRef.current = globalConfig?.hostname || globalConfig?.deviceName;

  // Update settings data when config changes
  useEffect(() => {
    setSettingsData({
      name: config?.name || "",
    });
  }, [config?.name]);

  const releaseFrame = () => {
    if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
      URL.revokeObjectURL(imageUrlRef.current);
      imageUrlRef.current = "";
      setImageUrl("");
    }
  };

  const stopMediaSocket = () => {
    const session = sessionRef.current;
    const ws = session?.getSocket();
    try {
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ cmd: "camera_stop" }));
      }
    } catch {
      // ignore
    }
    session?.stop();
  };

  useEffect(() => {
    isMountedRef.current = true;
    const session = createSocketSession({
      getUrl: () => buildWebSocketUrl("/ws/media", hostnameRef.current || null),
      binary: true,
      onStatus: (status) => {
        if (!isMountedRef.current) return;
        setStreamStatus(status);
        setIsStreaming(status === "connected");
      },
      onOpen: (ws) => {
        try {
          ws.send(JSON.stringify({ cmd: "camera_start" }));
        } catch {
          // ignore
        }
      },
      onMessage: (event) => {
        if (!(event.data instanceof ArrayBuffer)) return;
        const blob = new Blob([event.data], { type: "image/jpeg" });
        const objectURL = URL.createObjectURL(blob);
        if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
          URL.revokeObjectURL(imageUrlRef.current);
        }
        imageUrlRef.current = objectURL;
        setImageUrl(objectURL);
      },
    });
    sessionRef.current = session;
    return () => {
      isMountedRef.current = false;
      stopMediaSocket();
      session.dispose();
      sessionRef.current = null;
      if (imageUrlRef.current && imageUrlRef.current.startsWith("blob:")) {
        URL.revokeObjectURL(imageUrlRef.current);
      }
    };
  }, []);

  const handleStartStream = () => {
    setWantsStream(true);
    sessionRef.current?.start();
  };

  const handleStopStream = () => {
    setWantsStream(false);
    stopMediaSocket();
    setIsStreaming(false);
    setStreamStatus("disconnected");
    releaseFrame();
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
      // Enter fullscreen - use container instead of img to preserve rotation
      const element = containerRef.current || document.documentElement;

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

  const streamCaption = !wantsStream
    ? deviceOnline
      ? "Idle"
      : "Offline"
    : isStreaming
    ? "Live"
    : connectionLabel(streamStatus);

  return (
    <>
      <Module
        title={config?.name || SECTIONS.camera.title}
        icon={SECTIONS.camera.icon}
        moduleConfig={config}
        online={deviceOnline}
        onSettings={handleOpenSettings}
        settingsDisabled={!deviceOnline}
        settingsTooltip={"Settings"}
        sx={{
          "& .MuiCardContent-root": {
            minHeight: "auto",
            paddingBottom: "0px !important",
          },
        }}
      >
        <Box
          ref={containerRef}
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
            // Fullscreen styles
            "&:fullscreen": {
              width: "100vw",
              height: "100vh",
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              backgroundColor: "black",
            },
            "&:-webkit-full-screen": {
              width: "100vw",
              height: "100vh",
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              backgroundColor: "black",
            },
            "&:-moz-full-screen": {
              width: "100vw",
              height: "100vh",
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              backgroundColor: "black",
            },
            "&:-ms-fullscreen": {
              width: "100vw",
              height: "100vh",
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              backgroundColor: "black",
            },
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
                transform:
                  config?.rotation || globalConfig?.camera?.rotation
                    ? `rotate(${
                        config?.rotation || globalConfig?.camera?.rotation || 0
                      }deg)`
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
                backgroundColor: !wantsStream
                  ? deviceOnline
                    ? "primary.main"
                    : "error.main"
                  : isStreaming
                  ? "success.main"
                  : streamStatus === "error" || streamStatus === "offline"
                  ? "error.main"
                  : "warning.main",
              }}
            />
            <Typography variant="caption" sx={{ color: "white" }}>
              {streamCaption}
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
              wantsStream
                ? "Stop Stream"
                : !deviceOnline
                ? "Device Offline"
                : "Start Stream"
            }
          >
            <span>
              <IconButton
                onClick={wantsStream ? handleStopStream : handleStartStream}
                disabled={!deviceOnline && !wantsStream}
                sx={{
                  color:
                    !deviceOnline && !wantsStream
                      ? "text.disabled"
                      : "primary.main",
                }}
              >
                {wantsStream ? <PauseIcon /> : <PlayArrowIcon />}
              </IconButton>
            </span>
          </Tooltip>

          <Tooltip title={!deviceOnline ? "Device Offline" : "Take Snapshot"}>
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
              isFullscreen
                ? "Exit Fullscreen"
                : !deviceOnline
                ? "Device Offline"
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
