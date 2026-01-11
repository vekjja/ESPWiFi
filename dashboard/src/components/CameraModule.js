import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import { Box, Typography, IconButton, Tooltip } from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import FullscreenIcon from "@mui/icons-material/Fullscreen";
import FullscreenExitIcon from "@mui/icons-material/FullscreenExit";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import CameraSettingsModal from "./CameraSettingsModal";
import { buildWebSocketUrl, getFetchOptions } from "../utils/apiUtils";
import { resolveWebSocketUrl } from "../utils/connectionUtils";

export default function CameraModule({
  config,
  globalConfig,
  onUpdate,
  onDelete,
  deviceOnline = true,
  saveConfigToDevice,
  controlWsRef = null,
  registerControlBinaryHandler = null,
}) {
  const moduleKey = config?.key;
  const [isStreaming, setIsStreaming] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [streamUrl, setStreamUrl] = useState("");
  const [imageUrl, setImageUrl] = useState("");
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "",
    url: config?.url || "/ws/camera",
  });

  // Local websocket for non-default/custom camera endpoints only.
  const wsRef = useRef(null);
  const controlCameraModeRef = useRef(false);
  const controlUnsubRef = useRef(null);
  const controlStreamOnRef = useRef(false);
  const pendingSnapshotRef = useRef(false);
  const imgRef = useRef(null);
  const imageUrlRef = useRef("");
  const isMountedRef = useRef(true);

  // We intentionally avoid "online/status" polling/derivation here.
  // Start/stop is driven purely by `camera_subscribe` on the shared control WS.

  useEffect(() => {
    // Convert relative path to absolute URL
    let wsUrl = config?.url || "/ws/camera";
    const isDefaultCamera = wsUrl === "/ws/camera";

    // Check if URL already has a protocol
    if (!wsUrl.startsWith("ws://") && !wsUrl.startsWith("wss://")) {
      if (wsUrl.startsWith("/")) {
        // Camera now streams over /ws/control (binary frames) gated by a
        // camera_subscribe command. Keep /ws/camera as a logical default for
        // modules, but route it to control under the hood.
        if (isDefaultCamera) {
          wsUrl = resolveWebSocketUrl("control", globalConfig);
        } else {
          // Otherwise, keep existing behavior for custom endpoints.
          const mdnsHostname =
            globalConfig?.hostname || globalConfig?.deviceName;
          wsUrl = buildWebSocketUrl(wsUrl, mdnsHostname || null);
        }
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

    // Track whether this stream uses /ws/control camera_subscribe semantics.
    // (Used by start/stop to send subscribe/unsubscribe commands.)
    controlCameraModeRef.current = isDefaultCamera;
  }, [
    config?.url,
    globalConfig?.hostname,
    globalConfig?.deviceName,
    globalConfig?.cloudTunnel?.enabled,
    globalConfig?.cloudTunnel?.baseUrl,
    globalConfig?.auth?.token,
  ]); // Re-run if URL/device identity/cloud tunnel changes

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

  const handleStartStream = () => {
    if (!streamUrl) {
      console.error("No stream URL available");
      return;
    }

    // Default camera uses the shared /ws/control socket (no second WS).
    if (controlCameraModeRef.current) {
      const ws = controlWsRef?.current || null;
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        console.error(
          "Control socket not connected; cannot start camera stream"
        );
        setIsStreaming(false);
        return;
      }

      // Register binary handler (camera frames)
      if (typeof registerControlBinaryHandler === "function") {
        if (!controlUnsubRef.current) {
          controlUnsubRef.current = registerControlBinaryHandler((ab) => {
            if (!(ab instanceof ArrayBuffer)) return;
            // If this was a one-shot snapshot request, consume one frame and clear.
            if (pendingSnapshotRef.current) {
              pendingSnapshotRef.current = false;
              const blob = new Blob([ab], { type: "image/jpeg" });
              const objectUrl = URL.createObjectURL(blob);
              window.open(objectUrl, "_blank");
              setTimeout(() => URL.revokeObjectURL(objectUrl), 15000);
              return;
            }

            if (!controlStreamOnRef.current) return;
            const blob = new Blob([ab], { type: "image/jpeg" });
            const objectURL = URL.createObjectURL(blob);
            if (
              imageUrlRef.current &&
              imageUrlRef.current.startsWith("blob:")
            ) {
              URL.revokeObjectURL(imageUrlRef.current);
            }
            imageUrlRef.current = objectURL;
            setImageUrl(objectURL);
          });
        }
      }

      try {
        ws.send(JSON.stringify({ cmd: "camera_subscribe", enable: true }));
      } catch {
        // ignore
      }
      controlStreamOnRef.current = true;
      setIsStreaming(true);
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
        // Camera streams over /ws/control via a subscribe command.
        if (controlCameraModeRef.current) {
          try {
            ws.send(JSON.stringify({ cmd: "camera_subscribe", enable: true }));
          } catch {
            // ignore
          }
        }
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
        } else if (typeof event.data === "string") {
          // Control socket may send JSON responses; ignore for camera rendering.
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
    // Default camera: unsubscribe but do NOT close the shared control socket.
    if (controlCameraModeRef.current) {
      const ws = controlWsRef?.current || null;
      if (ws && ws.readyState === WebSocket.OPEN) {
        try {
          ws.send(JSON.stringify({ cmd: "camera_subscribe", enable: false }));
        } catch {
          // ignore
        }
      }
      controlStreamOnRef.current = false;
      if (controlUnsubRef.current) {
        try {
          controlUnsubRef.current();
        } catch {
          // ignore
        }
        controlUnsubRef.current = null;
      }
    } else if (wsRef.current) {
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
    try {
      // Snapshot is requested over /ws/control and returned as a binary JPEG frame.
      const ws = controlWsRef?.current || null;
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        throw new Error("control_ws_not_connected");
      }
      // Ensure we can receive the next binary frame.
      if (typeof registerControlBinaryHandler !== "function") {
        throw new Error("no_binary_handler");
      }
      if (!controlUnsubRef.current) {
        // Register handler so snapshot can be consumed even if not streaming.
        controlUnsubRef.current = registerControlBinaryHandler((ab) => {
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
      ws.send(JSON.stringify({ cmd: "camera_snapshot" }));
    } catch (err) {
      console.error("Failed to request snapshot:", err);
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
          <Tooltip
            title={isStreaming ? "Stop Stream" : "Start Stream"}
          >
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

          <Tooltip
            title={isFullscreen ? "Exit Fullscreen" : "Fullscreen"}
          >
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
