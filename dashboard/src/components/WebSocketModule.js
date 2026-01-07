import React, { useState, useEffect, useRef } from "react";
import { Typography, TextField, Button, Box } from "@mui/material";
import RestartAltIcon from "@mui/icons-material/RestartAlt";
import LinkIcon from "@mui/icons-material/Link";
import LinkOffIcon from "@mui/icons-material/LinkOff";
import Module from "./Module";
import WebSocketSettingsModal from "./WebSocketSettingsModal";
import { buildWebSocketUrl } from "../utils/apiUtils";

// Global connection manager to persist connections across re-renders
const connectionManager = new Map();

export default function WebSocketModule({
  index,
  initialProps,
  onUpdate,
  onDelete,
}) {
  // Use the module's key for stable identification
  const moduleKey = initialProps.key || index;

  const [message, setMessage] = useState("");
  const [connectionStatus, setConnectionStatus] = useState("disconnected");
  const [openModal, setOpenModal] = useState(false);
  const [inputText, setInputText] = useState("");

  // WebSocket settings data for the modal (only for editing)
  const [websocketSettingsData, setWebSocketSettingsData] = useState({
    name: initialProps.name || "",
    url: initialProps.url || "",
    payload: initialProps.payload || "text",
    fontSize: initialProps.fontSize || 14,
    enableSending: initialProps.enableSending !== false,
    imageRotation: initialProps.imageRotation || 0,
    size: initialProps.size || "medium",
    width: initialProps.width || 240,
    height: initialProps.height || 240,
  });

  // Use ref to store WebSocket instance
  const socketRef = useRef(null);
  // Use ref to preserve message across re-renders
  const messageRef = useRef("");

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      // Clean up object URL
      if (typeof message === "string" && message.startsWith("blob:")) {
        URL.revokeObjectURL(message);
      }

      // Close connection
      if (
        socketRef.current &&
        socketRef.current.readyState !== WebSocket.CLOSED
      ) {
        socketRef.current.close();
      }

      // Remove from connection manager
      connectionManager.delete(moduleKey);
    };
  }, []);

  // Auto-connect if previously connected (only on initial mount)
  // useEffect(() => {
  //   // Only auto-connect if explicitly set to connected in initialProps and no socket exists
  //   if (
  //     initialProps.connectionState === "connected" &&
  //     !socketRef.current &&
  //     !manuallyDisconnected
  //   ) {
  //     // Small delay to ensure component is fully mounted
  //     const timer = setTimeout(() => {
  //       // Double-check that we still need to connect
  //       if (
  //         initialProps.connectionState === "connected" &&
  //         !socketRef.current &&
  //         !manuallyDisconnected
  //       ) {
  //         createWebSocketConnection();
  //       }
  //     }, 100);
  //     return () => clearTimeout(timer);
  //   }
  // }, []); // Empty dependency array - only run on mount

  // Local connection state update (no config save)
  const updateConnectionState = (newStatus) => {
    if (newStatus !== connectionStatus) {
      setConnectionStatus(newStatus);

      // Save the updated connection state
      const updatedWebSocket = {
        ...websocketSettingsData,
        connectionState: newStatus,
      };
      onUpdate(moduleKey, updatedWebSocket);
    }
  };

  const createWebSocketConnection = () => {
    // Check if we already have a connection for this module
    const existingConnection = connectionManager.get(moduleKey);
    if (
      existingConnection &&
      existingConnection.readyState === WebSocket.OPEN
    ) {
      console.log(
        `WebSocket ${moduleKey} already connected, reusing existing connection`
      );
      socketRef.current = existingConnection;
      updateConnectionState("connected");
      return existingConnection;
    }

    // Close existing connection if any
    if (socketRef.current) {
      socketRef.current.close();
      socketRef.current = null;
    }

    let wsUrl = initialProps.url;

    // Convert relative path to absolute URL
    if (wsUrl.startsWith("/")) {
      // For relative paths, use the current hostname
      wsUrl = buildWebSocketUrl(wsUrl);
    } else if (!wsUrl.startsWith("ws://") && !wsUrl.startsWith("wss://")) {
      // If it's not a full WebSocket URL and not relative, assume it's a hostname
      // and add the default WebSocket protocol
      wsUrl = `ws://${wsUrl}`;
    }

    // Validate URL format
    try {
      new URL(wsUrl);
    } catch (error) {
      console.error(`Invalid WebSocket URL: ${wsUrl}`);
      updateConnectionState("error");
      return null;
    }

    console.log(`WebSocket`, moduleKey, "connecting:", wsUrl);

    try {
      const ws = new WebSocket(wsUrl);

      if (initialProps.payload === "binary") {
        ws.binaryType = "arraybuffer";
      }

      socketRef.current = ws;
      connectionManager.set(moduleKey, ws);
      updateConnectionState("connecting");

      ws.onopen = () => {
        console.log(`WebSocket`, moduleKey, "connected:", wsUrl);
        updateConnectionState("connected");
      };

      ws.onmessage = (event) => {
        if (initialProps.payload === "binary") {
          const blob = new Blob([event.data], { type: "image/jpeg" });
          const objectURL = URL.createObjectURL(blob);
          messageRef.current = objectURL;
          setMessage(objectURL);
        } else {
          messageRef.current = event.data;
          setMessage(event.data);
        }
      };

      ws.onerror = (error) => {
        console.error(`WebSocket ${moduleKey} error:`, error);
        updateConnectionState("error");
      };

      ws.onclose = (event) => {
        console.log(`WebSocket`, moduleKey, "disconnected:", wsUrl);
        updateConnectionState("disconnected");
        socketRef.current = null;
        connectionManager.delete(moduleKey);
      };

      return ws;
    } catch (error) {
      console.error(`Failed to create WebSocket for ${wsUrl}:`, error);
      updateConnectionState("error");
      return null;
    }
  };

  // Only sync modal state from props when opening the modal
  const handleSettingsClick = () => {
    setWebSocketSettingsData({
      name: initialProps.name || "",
      url: initialProps.url || "",
      payload: initialProps.payload || "text",
      fontSize: initialProps.fontSize || 14,
      enableSending: initialProps.enableSending !== false,
      imageRotation: initialProps.imageRotation || 0,
      size: initialProps.size || "medium",
      width: initialProps.width || 240,
      height: initialProps.height || 240,
    });
    setOpenModal(true);
  };

  const handleCloseModal = () => {
    setOpenModal(false);
  };

  const handleWebSocketDataChange = (changes) => {
    setWebSocketSettingsData((prev) => ({ ...prev, ...changes }));
  };

  const handleDeleteSocket = () => {
    // Best-effort cleanup before removing the module
    try {
      if (
        socketRef.current &&
        socketRef.current.readyState !== WebSocket.CLOSED
      ) {
        socketRef.current.close();
      }
    } catch {
      // ignore
    }
    socketRef.current = null;
    connectionManager.delete(moduleKey);
    onDelete(moduleKey);
    handleCloseModal();
  };

  const handleSaveSettings = () => {
    // Include connectionState when saving settings
    const updatedWebSocket = {
      type: initialProps.type,
      key: initialProps.key,
      url: websocketSettingsData.url,
      name: websocketSettingsData.name,
      payload: websocketSettingsData.payload,
      fontSize: Number(websocketSettingsData.fontSize),
      enableSending: websocketSettingsData.enableSending,
      imageRotation: Number(websocketSettingsData.imageRotation),
      connectionState: connectionStatus, // Save the current connection state
      size: websocketSettingsData.size,
      width: websocketSettingsData.width,
      height: websocketSettingsData.height,
    };

    onUpdate(moduleKey, updatedWebSocket);
    handleCloseModal();
  };

  const handleConnect = () => {
    createWebSocketConnection();
  };

  const handleDisconnect = () => {
    if (socketRef.current) {
      socketRef.current.close();
      socketRef.current = null;
      connectionManager.delete(moduleKey);
      // Immediately update the connection status manually
      updateConnectionState("disconnected");
    } else {
      // If there's no socket but status is still connected, fix the status
      if (connectionStatus === "connected") {
        updateConnectionState("disconnected");
      }
    }
  };

  const handleSend = () => {
    if (
      inputText.trim() &&
      socketRef.current &&
      socketRef.current.readyState === WebSocket.OPEN
    ) {
      socketRef.current.send(inputText);
      setInputText("");
    }
  };

  const handleKeyPress = (event) => {
    if (event.key === "Enter") {
      handleSend();
    }
  };

  // Remove the useEffect that syncs websocketSettingsData from initialProps on every prop change

  // Determine icon and tooltip based on connection status
  const getConnectionIcon = () => {
    switch (connectionStatus) {
      case "connected":
        return LinkIcon;
      case "connecting":
        return RestartAltIcon;
      case "error":
        return LinkOffIcon;
      default:
        return LinkOffIcon;
    }
  };

  const getConnectionTooltip = () => {
    switch (connectionStatus) {
      case "connected":
        return "Disconnect";
      case "connecting":
        return "Connecting...";
      case "error":
        return "Connection Error - Click to retry";
      default:
        return "Connect";
    }
  };

  // Size mapping
  const sizeMap = {
    small: { width: 180, height: 180 },
    medium: { width: 240, height: 240 },
    large: { width: 320, height: 320 },
  };
  const effectiveSize =
    websocketSettingsData.size === "custom"
      ? {
          width: websocketSettingsData.width,
          height: websocketSettingsData.height,
        }
      : sizeMap[websocketSettingsData.size] || sizeMap.medium;

  return (
    <Module
      title={initialProps.name || "WebSocket" + moduleKey}
      onSettings={handleSettingsClick}
      settingsTooltip="WebSocket Settings"
      onReconnect={
        connectionStatus === "connected" ? handleDisconnect : handleConnect
      }
      reconnectTooltip={getConnectionTooltip()}
      reconnectIcon={getConnectionIcon()}
      reconnectColor={connectionStatus === "connected" ? "primary" : "error"}
      sx={{
        backgroundColor:
          connectionStatus === "connected"
            ? "secondary.light"
            : "secondary.dark",
        borderColor:
          connectionStatus === "connected" ? "primary.main" : "secondary.main",
        minWidth: effectiveSize.width,
        maxWidth: effectiveSize.width,
        minHeight: effectiveSize.height,
        maxHeight: effectiveSize.height,
      }}
    >
      {/* Main content only, no input/send area here */}
      {connectionStatus === "connecting" ? (
        <div
          style={{
            display: "flex",
            justifyContent: "center",
            alignItems: "center",
            height: "120px",
          }}
        >
          <Typography variant="caption" color="text.secondary">
            Connecting...
          </Typography>
        </div>
      ) : connectionStatus === "error" ? (
        <div
          style={{
            display: "flex",
            justifyContent: "center",
            alignItems: "center",
            height: "120px",
          }}
        >
          <Typography variant="caption" color="error">
            Connection Error
          </Typography>
        </div>
      ) : connectionStatus === "disconnected" ? (
        <div
          style={{
            display: "flex",
            justifyContent: "center",
            alignItems: "center",
            height: "120px",
          }}
        >
          <Typography variant="caption" color="text.secondary">
            Disconnected
          </Typography>
        </div>
      ) : initialProps.payload === "binary" ? (
        messageRef.current &&
        typeof messageRef.current === "string" &&
        messageRef.current.startsWith("blob:") ? (
          <img
            src={messageRef.current}
            alt="WebSocket"
            style={{
              width: "100%",
              marginTop: "10px",
              maxHeight: "120px",
              objectFit: "contain",
              transform: `rotate(${initialProps.imageRotation || 0}deg)`,
            }}
          />
        ) : (
          <Typography variant="caption" color="text.secondary">
            Waiting for image...
          </Typography>
        )
      ) : (
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            // Remove height: "120px"
            minHeight: "60px", // Optional: just to keep a little space
          }}
        >
          <Typography
            variant="body2"
            sx={{
              wordBreak: "break-all",
              textAlign: "center",
              fontSize: initialProps.fontSize || 14,
              maxHeight: "120px",
              overflow: "auto",
            }}
          >
            {typeof messageRef.current === "string" ? messageRef.current : ""}
          </Typography>
        </Box>
      )}

      {/* Show send input and button below main content if enabled and connected */}
      {connectionStatus === "connected" &&
        initialProps.enableSending !== false && (
          <Box
            sx={{ mt: 1, mb: 1, px: 1, display: "flex", gap: 1, width: "100%" }}
          >
            <TextField
              label="Send message"
              value={inputText}
              onChange={(e) => setInputText(e.target.value)}
              onKeyDown={handleKeyPress}
              variant="outlined"
              size="small"
              fullWidth
              placeholder="Type a message..."
            />
            <Button
              onClick={handleSend}
              variant="contained"
              size="small"
              disabled={!inputText || !inputText.trim()}
            >
              Send
            </Button>
          </Box>
        )}

      <WebSocketSettingsModal
        open={openModal}
        onClose={handleCloseModal}
        onSave={handleSaveSettings}
        onDelete={handleDeleteSocket}
        websocketData={websocketSettingsData}
        onWebSocketDataChange={handleWebSocketDataChange}
      />
    </Module>
  );
}
