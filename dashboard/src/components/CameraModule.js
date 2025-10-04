import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import { Box, Typography, IconButton, Tooltip } from "@mui/material";
import {
  PlayArrow,
  Pause,
  Fullscreen,
  FullscreenExit,
} from "@mui/icons-material";

export default function CameraModule({ config, onUpdate, onDelete }) {
  const [isStreaming, setIsStreaming] = useState(false);
  const [isFullscreen, setIsFullscreen] = useState(false);
  const [streamUrl, setStreamUrl] = useState("");
  const [imageUrl, setImageUrl] = useState("");
  const wsRef = useRef(null);
  const imgRef = useRef(null);
  const imageUrlRef = useRef("");

  console.log("📷 CameraModule rendered with config:", config);
  console.log("📷 CameraModule apiURL:", config?.apiURL);

  useEffect(() => {
    // Convert relative path to absolute URL like WebSocketModule does
    let wsUrl = "/camera";

    if (wsUrl.startsWith("/")) {
      // For relative paths, use the current hostname
      const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
      const hostname = window.location.hostname;
      const port = window.location.port ? `:${window.location.port}` : "";
      wsUrl = `${protocol}//${hostname}${port}${wsUrl}`;
    }

    setStreamUrl(wsUrl);
    console.log("Camera WebSocket URL:", wsUrl);
  }, []); // Empty dependency array - only run once on mount

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

    console.log("🔌 Camera WebSocket connecting:", streamUrl);

    try {
      const ws = new WebSocket(streamUrl);
      ws.binaryType = "arraybuffer";

      wsRef.current = ws;
      setIsStreaming(false); // Set to connecting state

      ws.onopen = () => {
        console.log("✅ Camera WebSocket connected successfully");
        setIsStreaming(true);
      };

      ws.onmessage = (event) => {
        console.log("📷 Received camera frame, size:", event.data.byteLength);
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
        console.log(
          "❌ Camera WebSocket disconnected. Code:",
          event.code,
          "Reason:",
          event.reason
        );
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
    setIsFullscreen(!isFullscreen);
  };

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

  const handleSettings = () => {
    // Camera module settings could be expanded here
    console.log("Camera settings clicked");
  };

  return (
    <Module
      title="📷 Camera"
      onSettings={handleSettings}
      onDelete={onDelete}
      settingsTooltip="Camera Settings"
      deleteTooltip="Remove Camera Module"
      sx={{
        minWidth: isFullscreen ? "80vw" : "300px",
        maxWidth: isFullscreen ? "90vw" : "400px",
        minHeight: isFullscreen ? "60vh" : "250px",
        maxHeight: isFullscreen ? "80vh" : "400px",
      }}
    >
      <Box
        sx={{
          width: "100%",
          height: isFullscreen ? "calc(100% - 100px)" : "200px",
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
            <Typography
              variant="caption"
              sx={{ marginTop: 1, color: "primary.main" }}
            >
              WebSocket URL: {streamUrl}
            </Typography>
          </Box>
        )}

        {/* Stream controls overlay */}
        <Box
          sx={{
            position: "absolute",
            top: 8,
            right: 8,
            display: "flex",
            gap: 1,
            backgroundColor: "rgba(0, 0, 0, 0.7)",
            borderRadius: 1,
            padding: 0.5,
          }}
        >
          <Tooltip title={isStreaming ? "Stop Stream" : "Start Stream"}>
            <IconButton
              size="small"
              onClick={isStreaming ? handleStopStream : handleStartStream}
              sx={{ color: "white" }}
            >
              {isStreaming ? <Pause /> : <PlayArrow />}
            </IconButton>
          </Tooltip>

          <Tooltip title={isFullscreen ? "Exit Fullscreen" : "Fullscreen"}>
            <IconButton
              size="small"
              onClick={handleFullscreen}
              sx={{ color: "white" }}
            >
              {isFullscreen ? <FullscreenExit /> : <Fullscreen />}
            </IconButton>
          </Tooltip>
        </Box>

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
              backgroundColor: isStreaming ? "success.main" : "error.main",
            }}
          />
          <Typography variant="caption" sx={{ color: "white" }}>
            {isStreaming ? "Live" : "Offline"}
          </Typography>
        </Box>
      </Box>
    </Module>
  );
}
