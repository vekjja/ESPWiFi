import React, { useState, useEffect, useRef } from "react";
import Module from "../Module";
import InfoCard from "../../common/InfoCard";
import { Box, Typography, IconButton, Tooltip } from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import FullscreenIcon from "@mui/icons-material/Fullscreen";
import FullscreenExitIcon from "@mui/icons-material/FullscreenExit";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import CameraSettingsModal from "./CameraSettingsModal";
import { SECTIONS } from "../../common/SectionTitle";
import { buildWebSocketUrl, buildApiUrl } from "../../../utils/api";
import { attachCameraStream, cameraStreamRunning } from "./cameraStream";
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
  const [wantsStream, setWantsStream] = useState(() => cameraStreamRunning());
  const [streamStatus, setStreamStatus] = useState("disconnected");
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [imageUrl, setImageUrl] = useState("");
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "",
  });

  const streamRef = useRef(null);
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

  useEffect(() => {
    isMountedRef.current = true;
    const link = attachCameraStream({
      getUrl: () => buildWebSocketUrl("/ws/media", hostnameRef.current || null),
      onStatus: (status) => {
        if (!isMountedRef.current) return;
        setStreamStatus(status);
        setIsStreaming(status === "connected");
      },
      onFrame: (url) => {
        if (!isMountedRef.current) return;
        imageUrlRef.current = url;
        setImageUrl(url);
      },
    });
    streamRef.current = link;
    return () => {
      isMountedRef.current = false;
      link.detach();
      if (streamRef.current === link) streamRef.current = null;
    };
  }, []);

  const handleStartStream = () => {
    setWantsStream(true);
    streamRef.current?.start();
  };

  const handleStopStream = () => {
    setWantsStream(false);
    streamRef.current?.stop();
    setIsStreaming(false);
    setStreamStatus("disconnected");
    imageUrlRef.current = "";
    setImageUrl("");
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

  const rotation = Number(
    config?.rotation ?? globalConfig?.camera?.rotation ?? 0
  );
  const quarterTurn = Math.abs(rotation) % 180 === 90;

  return (
    <>
      <Module
        moduleConfig={config}
        online={deviceOnline}
        onResize={({ width, height }) =>
          onUpdate?.(moduleKey, { size: "custom", width, height })
        }
        onToggleWide={() => onUpdate?.(moduleKey, { wide: !config?.wide })}
        onDelete={handleDeleteModule}
        onSettings={handleOpenSettings}
        settingsDisabled={!deviceOnline}
        settingsTooltip={"Settings"}
      >
        <InfoCard
          embedded
          fluid
          scale
          title={config?.name || SECTIONS.camera.title}
          icon={SECTIONS.camera.icon}
        >
        <Box
          sx={{
            flex: "1 1 auto",
            minHeight: 0,
            minWidth: 0,
            width: "100%",
            display: "flex",
            flexDirection: "column",
            containerType: "size",
            containerName: "camera",
          }}
        >
        <Box
          ref={containerRef}
          sx={{
            width: "100%",
            flex: "1 1 auto",
            minHeight: 0,
            display: "flex",
            flexDirection: "column",
            alignItems: "center",
            justifyContent: "center",
            backgroundColor: "rgba(0, 0, 0, 0.3)",
            borderRadius: 1,
            position: "relative",
            overflow: "hidden",
            containerType: "size",
            containerName: "frame",
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
                position: "absolute",
                objectFit: "contain",
                borderRadius: "4px",
                transition: "transform 0.3s ease",
                ...(quarterTurn
                  ? {
                      top: "50%",
                      left: "50%",
                      width: "100cqh",
                      height: "100cqw",
                      transform: `translate(-50%, -50%) rotate(${rotation}deg)`,
                    }
                  : {
                      inset: 0,
                      width: "100%",
                      height: "100%",
                      transform: rotation ? `rotate(${rotation}deg)` : undefined,
                    }),
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
              <Typography
                sx={{
                  fontSize:
                    "clamp(0.85rem, 22cqh, calc(1.35rem * var(--module-scale, 1)))",
                  lineHeight: 1.15,
                  textAlign: "center",
                }}
              >
                📷 Camera {isStreaming ? "Live" : "Ready"}
              </Typography>
              <Typography
                sx={{
                  fontSize:
                    "clamp(0.7rem, 14cqh, calc(0.875rem * var(--module-scale, 1)))",
                  lineHeight: 1.15,
                  textAlign: "center",
                  "@container frame (max-height: 64px)": { display: "none" },
                }}
              >
                {/* {isStreaming ? "Streaming in progress" : "Click play to start streaming"} */}
              </Typography>
            </Box>
          )}

          {/* Stream status indicator */}
          <Box
            sx={{
              position: "absolute",
              bottom: "calc(8px * var(--module-scale, 1))",
              left: "calc(8px * var(--module-scale, 1))",
              display: "flex",
              alignItems: "center",
              gap: "calc(8px * var(--module-scale, 1))",
              backgroundColor: "rgba(0, 0, 0, 0.7)",
              borderRadius: 1,
              padding:
                "calc(4px * var(--module-scale, 1)) calc(8px * var(--module-scale, 1))",
              "@container frame (max-height: 72px)": { display: "none" },
            }}
          >
            <Box
              sx={{
                width: "calc(8px * var(--module-scale, 1))",
                height: "calc(8px * var(--module-scale, 1))",
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
            <Typography
              variant="caption"
              sx={{
                color: "white",
                fontSize: "calc(0.75rem * var(--module-scale, 1))",
                lineHeight: 1.1,
              }}
            >
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
            flexWrap: "wrap",
            gap: "calc(8px * var(--module-scale, 1))",
            padding: "calc(4px * var(--module-scale, 1))",
            backgroundColor: "rgba(0, 0, 0, 0.1)",
            borderRadius: 1,
            marginTop: "calc(4px * var(--module-scale, 1))",
            flex: "0 0 auto",
            "& .MuiIconButton-root": {
              padding: "calc(8px * var(--module-scale, 1))",
            },
            "& .MuiSvgIcon-root": {
              fontSize: "calc(24px * var(--module-scale, 1))",
            },
            "@container camera (max-height: 180px)": {
              marginTop: "2px",
              padding: "2px",
              gap: "2px",
              "& .MuiIconButton-root": { padding: "4px" },
              "& .MuiSvgIcon-root": { fontSize: 20 },
            },
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
        </Box>
        </InfoCard>
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
