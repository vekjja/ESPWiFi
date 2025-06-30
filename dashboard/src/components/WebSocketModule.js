import React, { useState, useEffect, useRef } from "react";
import {
  Typography,
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  Button,
  Box,
  FormControlLabel,
  Checkbox,
} from "@mui/material";
import { RestartAlt, Link, LinkOff } from "@mui/icons-material";
import DeleteButton from "./DeleteButton";
import OkayButton from "./OkayButton";
import Module from "./Module";
import SettingsModal from "./SettingsModal";

// Global connection manager to persist connections across re-renders
const connectionManager = new Map();

export default function WebSocketModule({
  index,
  initialProps,
  onUpdate,
  onDelete,
}) {
  // Use the module's id for stable identification
  const moduleId = initialProps.id || index;

  const [message, setMessage] = useState("");
  const [connectionStatus, setConnectionStatus] = useState(
    initialProps.connectionState || "disconnected"
  );
  const [openModal, setOpenModal] = useState(false);
  const [editedURL, setEditedURL] = useState(initialProps.url || "");
  const [editedName, setEditedName] = useState(initialProps.name || "");
  const [editedPayload, setEditedPayload] = useState(
    initialProps.payload || "text"
  );
  const [editedFontSize, setEditedFontSize] = useState(
    initialProps.fontSize || 14
  );
  const [editedEnableSending, setEditedEnableSending] = useState(
    initialProps.enableSending !== false
  );
  const [inputText, setInputText] = useState("");
  const [manuallyDisconnected, setManuallyDisconnected] = useState(false);

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
      connectionManager.delete(moduleId);

      // Clean up debounce timeout
      if (saveConnectionState.timeoutId) {
        clearTimeout(saveConnectionState.timeoutId);
      }
    };
  }, []);

  // Auto-connect if previously connected
  useEffect(() => {
    // Only auto-connect if explicitly set to connected, no socket exists, and not manually disconnected
    if (
      initialProps.connectionState === "connected" &&
      !socketRef.current &&
      !manuallyDisconnected
    ) {
      // Small delay to ensure component is fully mounted
      const timer = setTimeout(() => {
        // Double-check that we still need to connect
        if (
          initialProps.connectionState === "connected" &&
          !socketRef.current &&
          !manuallyDisconnected
        ) {
          createWebSocketConnection();
        }
      }, 100);
      return () => clearTimeout(timer);
    }
  }, [initialProps.connectionState, manuallyDisconnected]); // Remove url dependency to prevent reconnection loops

  // Save connection state to config whenever it changes
  const saveConnectionState = (newStatus) => {
    // Only update if status actually changed
    if (newStatus === connectionStatus) {
      return;
    }

    setConnectionStatus(newStatus);

    // Debounce config updates to prevent excessive saves
    clearTimeout(saveConnectionState.timeoutId);
    saveConnectionState.timeoutId = setTimeout(() => {
      const updatedWebSocket = {
        ...initialProps,
        connectionState: newStatus,
      };
      onUpdate(moduleId, updatedWebSocket);
    }, 500); // 500ms debounce
  };

  // Manual state update (for disconnect button)
  const updateConnectionState = (newStatus) => {
    setConnectionStatus(newStatus);
  };

  const createWebSocketConnection = () => {
    // Check if we already have a connection for this module
    const existingConnection = connectionManager.get(moduleId);
    if (
      existingConnection &&
      existingConnection.readyState === WebSocket.OPEN
    ) {
      console.log(
        `WebSocket ${moduleId} already connected, reusing existing connection`
      );
      socketRef.current = existingConnection;
      setConnectionStatus("connected");
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
      const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
      const hostname = window.location.hostname;
      const port = window.location.port ? `:${window.location.port}` : "";
      wsUrl = `${protocol}//${hostname}${port}${wsUrl}`;
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
      saveConnectionState("error");
      return null;
    }

    console.log(`WebSocket ${moduleId} connecting to: ${wsUrl}`);

    try {
      const ws = new WebSocket(wsUrl);

      if (initialProps.payload === "binary") {
        ws.binaryType = "arraybuffer";
      }

      socketRef.current = ws;
      connectionManager.set(moduleId, ws);
      saveConnectionState("connecting");

      ws.onopen = () => {
        console.log(`WebSocket ${moduleId} connected to: ${wsUrl}`);
        // Force update to connected status regardless of current state
        setConnectionStatus("connected");

        // Debounce the config update
        clearTimeout(saveConnectionState.timeoutId);
        saveConnectionState.timeoutId = setTimeout(() => {
          const updatedWebSocket = {
            ...initialProps,
            connectionState: "connected",
          };
          onUpdate(moduleId, updatedWebSocket);
        }, 500); // 500ms debounce
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
        console.error(`WebSocket ${moduleId} error:`, error);
        saveConnectionState("error");
      };

      ws.onclose = (event) => {
        console.log(`WebSocket ${moduleId} disconnected from: ${wsUrl}`);
        saveConnectionState("disconnected");
        socketRef.current = null;
        connectionManager.delete(moduleId);
      };

      return ws;
    } catch (error) {
      console.error(`Failed to create WebSocket for ${wsUrl}:`, error);
      saveConnectionState("error");
      return null;
    }
  };

  const handleSettingsClick = () => {
    setOpenModal(true);
  };

  const handleCloseModal = () => {
    setOpenModal(false);
  };

  const handleDeleteSocket = () => {
    onDelete(moduleId);
    handleCloseModal();
  };

  const handleSaveSettings = () => {
    const updatedWebSocket = {
      ...initialProps,
      url: editedURL,
      name: editedName,
      payload: editedPayload,
      fontSize: Number(editedFontSize),
      enableSending: editedEnableSending,
      connectionState: connectionStatus,
    };

    onUpdate(moduleId, updatedWebSocket);
    handleCloseModal();
  };

  const handleConnect = () => {
    console.log(`WebSocket ${moduleId} connect button clicked`);
    setManuallyDisconnected(false); // Reset manual disconnect flag
    createWebSocketConnection();
  };

  const handleDisconnect = () => {
    console.log(`WebSocket ${moduleId} disconnect button clicked`);
    setManuallyDisconnected(true); // Set manual disconnect flag
    if (socketRef.current) {
      socketRef.current.close();
      socketRef.current = null;
      connectionManager.delete(moduleId);
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

  useEffect(() => {
    setEditedURL(initialProps.url || "");
    setEditedName(initialProps.name || "");
    setEditedPayload(initialProps.payload || "text");
    setEditedFontSize(initialProps.fontSize || 14);
    setEditedEnableSending(initialProps.enableSending !== false);

    // Update connection status if it changed in config (but don't trigger save)
    if (
      initialProps.connectionState &&
      initialProps.connectionState !== connectionStatus
    ) {
      setConnectionStatus(initialProps.connectionState);
    }

    // Restore message from ref if it exists
    if (messageRef.current && messageRef.current !== message) {
      setMessage(messageRef.current);
    }
  }, [initialProps]);

  // Determine icon and tooltip based on connection status
  const getConnectionIcon = () => {
    switch (connectionStatus) {
      case "connected":
        return Link;
      case "connecting":
        return RestartAlt;
      case "error":
        return LinkOff;
      default:
        return LinkOff;
    }
  };

  const getConnectionTooltip = () => {
    switch (connectionStatus) {
      case "connected":
        return "Disconnect WebSocket";
      case "connecting":
        return "Connecting...";
      case "error":
        return "Connection Error - Click to retry";
      default:
        return "Connect WebSocket";
    }
  };

  return (
    <Module
      title={initialProps.name || "Unnamed"}
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
        maxWidth: "200px",
      }}
    >
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
            }}
          />
        ) : (
          <Typography variant="caption" color="text.secondary">
            Waiting for image...
          </Typography>
        )
      ) : (
        <Typography
          variant="body2"
          sx={{
            marginTop: "10px",
            wordBreak: "break-all",
            maxHeight: "120px",
            overflow: "auto",
            textAlign: "center",
            fontSize: initialProps.fontSize || 14,
          }}
        >
          {typeof messageRef.current === "string" ? messageRef.current : ""}
        </Typography>
      )}

      {connectionStatus === "connected" &&
        initialProps.enableSending !== false && (
          <Box sx={{ marginTop: 2, display: "flex", gap: 1 }}>
            <TextField
              label="Send message"
              value={inputText}
              onChange={(e) => setInputText(e.target.value)}
              onKeyPress={handleKeyPress}
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

      <SettingsModal
        open={openModal}
        onClose={handleCloseModal}
        title="WebSocket Settings"
        actions={
          <>
            <DeleteButton
              onClick={handleDeleteSocket}
              tooltip="Delete WebSocket"
            />
            <OkayButton
              onClick={handleSaveSettings}
              tooltip="Apply WebSocket Settings"
            />
          </>
        }
      >
        <TextField
          label="Name"
          value={editedName}
          onChange={(e) => setEditedName(e.target.value)}
          variant="outlined"
          fullWidth
          sx={{ marginBottom: 2 }}
        />
        <TextField
          label="WebSocket URL"
          value={editedURL}
          onChange={(e) => setEditedURL(e.target.value)}
          variant="outlined"
          fullWidth
          sx={{ marginBottom: 2 }}
          helperText="Use relative path (e.g., /rssi) for same server, or full URL for external WebSockets"
        />
        <FormControl fullWidth sx={{ marginBottom: 2 }}>
          <InputLabel id="websocket-payload-select-label">Payload</InputLabel>
          <Select
            labelId="websocket-payload-select-label"
            value={editedPayload}
            label="Payload"
            onChange={(e) => setEditedPayload(e.target.value)}
          >
            <MenuItem value="binary">Binary</MenuItem>
            <MenuItem value="text">Text</MenuItem>
          </Select>
        </FormControl>
        <TextField
          label="Font Size (px)"
          type="number"
          value={editedFontSize}
          onChange={(e) => setEditedFontSize(e.target.value)}
          variant="outlined"
          fullWidth
          sx={{ marginBottom: 2 }}
        />
        <FormControlLabel
          control={
            <Checkbox
              checked={editedEnableSending}
              onChange={(e) => setEditedEnableSending(e.target.checked)}
            />
          }
          label="Enable Sending"
        />
      </SettingsModal>
    </Module>
  );
}
