import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import {
  Box,
  Typography,
  IconButton,
  Tooltip,
  TextField,
  Slider,
} from "@mui/material";
import {
  PlayArrow,
  Pause,
  Fullscreen,
  FullscreenExit,
  CameraAlt,
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
}) {
  const [isStreaming, setIsStreaming] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [streamUrl, setStreamUrl] = useState("");
  const [imageUrl, setImageUrl] = useState("");
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "",
    url: config?.url || "/camera",
    frameRate: config?.frameRate || 10,
  });
  const wsRef = useRef(null);
  const imgRef = useRef(null);
  const imageUrlRef = useRef("");

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
    });
    setSettingsModalOpen(true);
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
                📷 Camera Offline
              </Typography>
              <Typography variant="body2">
                Click play to start streaming
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
                backgroundColor: isStreaming ? "primary.main" : "error.main",
              }}
            />
            <Typography variant="caption" sx={{ color: "white" }}>
              {isStreaming ? "Live" : "Offline"}
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
            <IconButton
              onClick={isStreaming ? handleStopStream : handleStartStream}
              sx={{ color: "primary.main" }}
            >
              {isStreaming ? <Pause /> : <PlayArrow />}
            </IconButton>
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
        </Box>
      </SettingsModal>
    </>
  );
}
