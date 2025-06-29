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

export default function WebSocketModule({
  index,
  initialProps,
  onUpdate,
  onDelete,
}) {
  const [message, setMessage] = useState("");
  const [connectionStatus, setConnectionStatus] = useState(
    initialProps.connectionState || "disconnected"
  );
  const [openModal, setOpenModal] = useState(false);
  const [editedURL, setEditedURL] = useState(initialProps.url || "");
  const [editedName, setEditedName] = useState(initialProps.name || "");
  const [editedType, setEditedType] = useState(initialProps.type || "text");
  const [editedFontSize, setEditedFontSize] = useState(
    initialProps.fontSize || 14
  );
  const [editedEnableSending, setEditedEnableSending] = useState(
    initialProps.enableSending !== false
  );
  const [inputText, setInputText] = useState("");

  // Use ref to store WebSocket instance
  const socketRef = useRef(null);

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
    };
  }, []);

  // Auto-connect if previously connected
  useEffect(() => {
    if (initialProps.connectionState === "connected" && !socketRef.current) {
      // Small delay to ensure component is fully mounted
      const timer = setTimeout(() => {
        // Double-check that we still need to connect
        if (
          initialProps.connectionState === "connected" &&
          !socketRef.current
        ) {
          createWebSocketConnection();
        }
      }, 100);
      return () => clearTimeout(timer);
    }
  }, [initialProps.connectionState, initialProps.url]); // Add url as dependency

  // Save connection state to config whenever it changes
  const saveConnectionState = (newStatus) => {
    setConnectionStatus(newStatus);
    const updatedWebSocket = {
      ...initialProps,
      connectionState: newStatus,
    };
    onUpdate(index, updatedWebSocket);
  };

  const createWebSocketConnection = () => {
    // Close existing connection if any
    if (socketRef.current) {
      socketRef.current.close();
      socketRef.current = null;
    }

    let wsUrl = initialProps.url;

    // Convert relative path to absolute URL
    if (wsUrl.startsWith("/")) {
      const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
      const hostname = window.location.hostname;
      const port = window.location.port ? `:${window.location.port}` : "";
      wsUrl = `${protocol}//${hostname}${port}${wsUrl}`;
    }

    console.log(`Creating WebSocket connection for index ${index} to ${wsUrl}`);

    try {
      const ws = new WebSocket(wsUrl);

      if (initialProps.type === "binary") {
        ws.binaryType = "arraybuffer";
      }

      socketRef.current = ws;
      saveConnectionState("connecting");

      ws.onopen = () => {
        console.log(`WebSocket ${index} connected`);
        saveConnectionState("connected");
      };

      ws.onmessage = (event) => {
        if (initialProps.type === "binary") {
          const blob = new Blob([event.data], { type: "image/jpeg" });
          const objectURL = URL.createObjectURL(blob);
          setMessage(objectURL);
        } else {
          setMessage(event.data);
        }
      };

      ws.onerror = (error) => {
        console.error(`WebSocket ${index} error:`, error);
        saveConnectionState("error");
      };

      ws.onclose = () => {
        console.log(`WebSocket ${index} closed`);
        saveConnectionState("disconnected");
        socketRef.current = null;
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
    onDelete(index);
    handleCloseModal();
  };

  const handleSaveSettings = () => {
    const updatedWebSocket = {
      ...initialProps,
      url: editedURL,
      name: editedName,
      type: editedType,
      fontSize: Number(editedFontSize),
      enableSending: editedEnableSending,
      connectionState: connectionStatus,
    };

    onUpdate(index, updatedWebSocket);
    handleCloseModal();
  };

  const handleConnect = () => {
    createWebSocketConnection();
  };

  const handleDisconnect = () => {
    if (socketRef.current) {
      socketRef.current.close();
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
    setEditedType(initialProps.type || "text");
    setEditedFontSize(initialProps.fontSize || 14);
    setEditedEnableSending(initialProps.enableSending !== false);

    // Update connection status if it changed in config (but don't trigger save)
    if (
      initialProps.connectionState &&
      initialProps.connectionState !== connectionStatus
    ) {
      setConnectionStatus(initialProps.connectionState);
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
      ) : initialProps.type === "binary" ? (
        message &&
        typeof message === "string" &&
        message.startsWith("blob:") ? (
          <img
            src={message}
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
          {typeof message === "string" ? message : ""}
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
          <InputLabel id="websocket-type-select-label">Type</InputLabel>
          <Select
            labelId="websocket-type-select-label"
            value={editedType}
            label="Type"
            onChange={(e) => setEditedType(e.target.value)}
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
