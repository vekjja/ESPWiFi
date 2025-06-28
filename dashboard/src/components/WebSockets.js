import React, { useState, useEffect, useRef, useMemo } from "react";
import {
  Container,
  Typography,
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  CircularProgress,
  Button,
  Box,
  FormControlLabel,
  Checkbox,
} from "@mui/material";
import { RestartAlt, LinkOff } from "@mui/icons-material";
import DeleteButton from "./DeleteButton";
import SaveButton from "./SaveButton";
import IButton from "./IButton";
import Module from "./Module";
import SettingsModal from "./SettingsModal";

export default function WebSockets({ config, saveConfig }) {
  const [webSockets, setWebSockets] = useState([]);
  const [messages, setMessages] = useState([]);
  const [connectionStatus, setConnectionStatus] = useState({});
  const [openModal, setOpenModal] = useState(false);
  const [selectedWebSocket, setSelectedWebSocket] = useState(null);
  const [editedURL, setEditedURL] = useState("");
  const [editedName, setEditedName] = useState("");
  const [editedType, setEditedType] = useState("text");
  const [editedFontSize, setEditedFontSize] = useState(14);
  const [editedEnableSending, setEditedEnableSending] = useState(true);
  const [inputTexts, setInputTexts] = useState({});

  // Use refs to store WebSocket instances for proper cleanup
  const socketRefs = useRef({});
  const prevWebSocketConfigRef = useRef(null);

  // Create a stable reference for WebSocket configuration
  const webSocketConfig = useMemo(() => {
    if (config && config.webSockets) {
      return config.webSockets.map((ws) => ({
        ...ws,
        type: ws.type || "text",
        fontSize: ws.fontSize || 14,
        enableSending: ws.enableSending !== false,
        connectionState: ws.connectionState || "disconnected",
      }));
    }
    return [];
  }, [
    config?.webSockets
      ? JSON.stringify(
          config.webSockets.map((ws) => ({
            url: ws.url,
            type: ws.type || "text",
            connectionState: ws.connectionState || "disconnected",
          }))
        )
      : null,
  ]);

  useEffect(() => {
    // Only update if the configuration has actually changed
    const currentConfigString = JSON.stringify(
      webSockets.map((ws) => ({
        url: ws.url,
        type: ws.type || "text",
        connectionState: ws.connectionState || "disconnected",
      }))
    );
    const newConfigString = JSON.stringify(
      webSocketConfig.map((ws) => ({
        url: ws.url,
        type: ws.type || "text",
        connectionState: ws.connectionState || "disconnected",
      }))
    );

    if (currentConfigString !== newConfigString) {
      console.log("Updating WebSocket configuration from props");
      setWebSockets(webSocketConfig);
    }
  }, [webSocketConfig]);

  // Function to update connection state in config
  const updateConnectionStateInConfig = (index, newState) => {
    setConnectionStatus((prev) => ({
      ...prev,
      [index]: newState,
    }));

    // Update the WebSocket configuration with the new connection state
    const updatedWebSockets = [...webSockets];
    if (updatedWebSockets[index]) {
      updatedWebSockets[index] = {
        ...updatedWebSockets[index],
        connectionState: newState,
      };

      // Update local state
      setWebSockets(updatedWebSockets);

      // Update config
      const updatedConfig = { ...config, webSockets: updatedWebSockets };
      saveConfig(updatedConfig);
    }
  };

  // Function to create a WebSocket connection for a specific index
  const createWebSocketConnection = (index, webSocket) => {
    // Check if we already have a connection for this index
    if (socketRefs.current[index]) {
      console.log(
        `WebSocket connection already exists for index ${index}, skipping creation`
      );
      return socketRefs.current[index];
    }

    console.log(
      `Creating WebSocket connection for index ${index} to ${webSocket.url}`
    );

    // Set loading state for this socket
    updateConnectionStateInConfig(index, "connecting");

    const ws = new WebSocket(webSocket.url);
    if (webSocket.type === "binary") {
      ws.binaryType = "arraybuffer";
    }

    // Store the WebSocket instance in refs for cleanup
    socketRefs.current[index] = ws;

    ws.onopen = () => {
      console.log(`WebSocket ${index} connected successfully`);
      updateConnectionStateInConfig(index, "connected");
    };

    ws.onmessage = (event) => {
      if (webSocket.type === "binary") {
        const blob = new Blob([event.data], { type: "image/jpeg" });
        const objectURL = URL.createObjectURL(blob);
        setMessages((prev) => {
          const updated = [...prev];
          updated[index] = objectURL;
          return updated;
        });
      } else {
        setMessages((prev) => {
          const updated = [...prev];
          updated[index] = event.data;
          return updated;
        });
      }
    };

    ws.onerror = (error) => {
      console.error(`WebSocket ${index} error:`, error);
      updateConnectionStateInConfig(index, "error");
    };

    ws.onclose = (event) => {
      console.log(
        `WebSocket ${index} closed with code: ${event.code}, reason: ${event.reason}`
      );

      // Only update status if this is still the current WebSocket for this index
      if (socketRefs.current[index] === ws) {
        updateConnectionStateInConfig(index, "disconnected");
        delete socketRefs.current[index];
      }
    };

    return ws;
  };

  useEffect(() => {
    // Check if the WebSocket configuration has actually changed
    const currentConfigString = JSON.stringify(
      webSockets.map((ws) => ({
        url: ws.url,
        type: ws.type || "text",
        connectionState: ws.connectionState || "disconnected",
      }))
    );
    const prevConfigString = prevWebSocketConfigRef.current;

    if (currentConfigString === prevConfigString) {
      console.log("WebSocket configuration unchanged, skipping reconnection");
      return;
    }

    console.log("WebSocket configuration changed, checking for differences");
    prevWebSocketConfigRef.current = currentConfigString;

    // Parse the previous configuration to compare
    let prevConfig = [];
    try {
      prevConfig = prevConfigString ? JSON.parse(prevConfigString) : [];
    } catch (e) {
      prevConfig = [];
    }

    // Compare configurations and only update changed WebSockets
    webSockets.forEach((webSocket, index) => {
      const prevWebSocket = prevConfig[index];

      // Check if this WebSocket is new, modified, or unchanged
      const isNew = !prevWebSocket;
      const isModified =
        prevWebSocket &&
        (prevWebSocket.url !== webSocket.url ||
          prevWebSocket.type !== webSocket.type);

      if (isNew) {
        console.log(`Creating new WebSocket connection for index ${index}`);
        createWebSocketConnection(index, webSocket);
      } else if (isModified) {
        console.log(
          `Updating WebSocket connection for index ${index} - configuration changed`
        );
        // Close existing connection if it exists
        if (
          socketRefs.current[index] &&
          socketRefs.current[index].readyState !== WebSocket.CLOSED
        ) {
          socketRefs.current[index].close(1000, "Configuration changed");
        }
        // Clear the message for this index
        setMessages((prev) => {
          const updated = [...prev];
          if (
            updated[index] &&
            typeof updated[index] === "string" &&
            updated[index].startsWith("blob:")
          ) {
            URL.revokeObjectURL(updated[index]);
          }
          updated[index] = null;
          return updated;
        });
        // Create new connection
        createWebSocketConnection(index, webSocket);
      } else {
        console.log(
          `WebSocket at index ${index} unchanged, keeping existing connection`
        );
      }
    });

    // Remove WebSockets that no longer exist
    prevConfig.forEach((prevWebSocket, index) => {
      if (!webSockets[index]) {
        console.log(`Removing WebSocket connection for index ${index}`);
        if (
          socketRefs.current[index] &&
          socketRefs.current[index].readyState !== WebSocket.CLOSED
        ) {
          socketRefs.current[index].close(1000, "WebSocket removed");
        }
        delete socketRefs.current[index];

        // Clear the message for this index
        setMessages((prev) => {
          const updated = [...prev];
          if (
            updated[index] &&
            typeof updated[index] === "string" &&
            updated[index].startsWith("blob:")
          ) {
            URL.revokeObjectURL(updated[index]);
          }
          updated[index] = null;
          return updated;
        });

        // Clear connection status
        setConnectionStatus((prev) => {
          const updated = { ...prev };
          delete updated[index];
          return updated;
        });
      }
    });
  }, [webSockets]); // Only recreate when webSockets array changes

  // Initialize connection status from config on component mount
  useEffect(() => {
    if (webSockets.length > 0) {
      const initialConnectionStatus = {};
      webSockets.forEach((webSocket, index) => {
        initialConnectionStatus[index] =
          webSocket.connectionState || "disconnected";
      });
      setConnectionStatus(initialConnectionStatus);
    }
  }, [webSockets]);

  // Separate cleanup effect for component unmount
  useEffect(() => {
    return () => {
      console.log(
        "Component unmounting, cleaning up all WebSocket connections"
      );
      // Clean up object URLs to prevent memory leaks
      setMessages((prev) => {
        prev.forEach((message, idx) => {
          if (typeof message === "string" && message.startsWith("blob:")) {
            URL.revokeObjectURL(message);
          }
        });
        return [];
      });

      Object.values(socketRefs.current).forEach((ws) => {
        if (ws && ws.readyState !== WebSocket.CLOSED) {
          ws.close(1000, "Component unmount");
        }
      });
      socketRefs.current = {};
    };
  }, []); // Only run on component unmount

  const handleSettingsClick = (webSocket) => {
    setSelectedWebSocket(webSocket);
    setOpenModal(true);
  };

  const handleCloseModal = () => {
    setOpenModal(false);
    setSelectedWebSocket(null);
  };

  const handleDeleteSocket = () => {
    const updatedWebSockets = webSockets.filter(
      (ws) => ws !== selectedWebSocket
    );
    updateWebSocketArray(updatedWebSockets);
    handleCloseModal();
  };

  const handleSaveSettings = () => {
    // Find the index of the selected WebSocket
    const selectedIndex = webSockets.findIndex(
      (ws) => ws === selectedWebSocket
    );

    if (selectedIndex === -1) {
      console.error("Selected WebSocket not found");
      handleCloseModal();
      return;
    }

    // Create the updated WebSocket configuration
    const updatedWebSocket = {
      ...selectedWebSocket,
      url: editedURL,
      name: editedName,
      type: editedType,
      fontSize: Number(editedFontSize),
      enableSending: editedEnableSending,
    };

    // Update only the specific WebSocket in the array
    const updatedWebSockets = [...webSockets];
    updatedWebSockets[selectedIndex] = updatedWebSocket;

    updateWebSocketArray(updatedWebSockets);
    handleCloseModal();
  };

  const handleSend = (index) => {
    const text = inputTexts[index] || "";
    if (
      text.trim() &&
      socketRefs.current[index] &&
      socketRefs.current[index].readyState === WebSocket.OPEN
    ) {
      socketRefs.current[index].send(text);
      // Clear the input after sending
      setInputTexts((prev) => ({
        ...prev,
        [index]: "",
      }));
    }
  };

  const handleInputChange = (index, value) => {
    setInputTexts((prev) => ({
      ...prev,
      [index]: value,
    }));
  };

  const handleKeyPress = (index, event) => {
    if (event.key === "Enter") {
      handleSend(index);
    }
  };

  const handleReconnect = () => {
    if (selectedWebSocket) {
      // Find the index of the selected WebSocket
      const index = webSockets.findIndex((ws) => ws === selectedWebSocket);
      if (index !== -1) {
        // Close the current connection with a normal closure code
        if (
          socketRefs.current[index] &&
          socketRefs.current[index].readyState !== WebSocket.CLOSED
        ) {
          socketRefs.current[index].close(1000, "Manual reconnect");
        }

        // Set connecting status
        updateConnectionStateInConfig(index, "connecting");

        // Add timeout before reconnection attempt
        setTimeout(() => {
          // Only create new connection if we don't already have one
          if (!socketRefs.current[index]) {
            console.log(`Manual reconnection for index ${index}`);
            createWebSocketConnection(index, selectedWebSocket);
          }
        }, 1000);
      }
    }

    // Close the modal after initiating reconnection
    handleCloseModal();
  };

  const handleDisconnect = () => {
    if (selectedWebSocket) {
      // Find the index of the selected WebSocket
      const index = webSockets.findIndex((ws) => ws === selectedWebSocket);
      if (index !== -1) {
        // Close the current connection with a normal closure code to prevent auto-reconnect
        if (
          socketRefs.current[index] &&
          socketRefs.current[index].readyState !== WebSocket.CLOSED
        ) {
          socketRefs.current[index].close(1000, "Manual disconnect");
        }

        // Set disconnected status
        updateConnectionStateInConfig(index, "disconnected");
      }
    }

    // Close the modal after disconnecting
    handleCloseModal();
  };

  useEffect(() => {
    if (selectedWebSocket) {
      setEditedURL(selectedWebSocket.url);
      setEditedName(selectedWebSocket.name || "");
      setEditedType(selectedWebSocket.type || "text");
      setEditedFontSize(selectedWebSocket.fontSize || 14);
      setEditedEnableSending(selectedWebSocket.enableSending !== false);
    }
  }, [selectedWebSocket]);

  // Function to safely update WebSocket array
  const updateWebSocketArray = (newWebSockets) => {
    console.log(
      "Updating WebSocket array:",
      newWebSockets.length,
      "connections"
    );

    // Preserve connection states for existing WebSockets
    const updatedWebSockets = newWebSockets.map((ws, index) => {
      const existingWebSocket = webSockets[index];
      return {
        ...ws,
        connectionState: existingWebSocket?.connectionState || "disconnected",
      };
    });

    setWebSockets(updatedWebSockets);

    // Update local config
    const updatedConfig = { ...config, webSockets: updatedWebSockets };
    saveConfig(updatedConfig);
  };

  return (
    <Container
      sx={{
        marginTop: 2,
        display: "flex",
        flexWrap: "wrap",
        justifyContent: "center",
      }}
    >
      {webSockets.map((webSocket, index) => (
        <Module
          key={index}
          title={webSocket.name || "Unnamed"}
          onSettings={() => handleSettingsClick(webSocket)}
          settingsTooltip={"Websocket Settings"}
        >
          {connectionStatus[index] === "connecting" ? (
            <div
              style={{
                display: "flex",
                justifyContent: "center",
                alignItems: "center",
                height: "120px",
                flexDirection: "column",
              }}
            >
              <CircularProgress size={24} sx={{ marginBottom: 1 }} />
              <Typography variant="caption" color="text.secondary">
                Connecting...
              </Typography>
            </div>
          ) : connectionStatus[index] === "error" ? (
            <div
              style={{
                display: "flex",
                justifyContent: "center",
                alignItems: "center",
                height: "120px",
                flexDirection: "column",
              }}
            >
              <Typography variant="caption" color="error">
                Connection Error
              </Typography>
            </div>
          ) : connectionStatus[index] === "disconnected" ||
            connectionStatus[index] === "📶" ? (
            <div
              style={{
                display: "flex",
                justifyContent: "center",
                alignItems: "center",
                height: "120px",
                flexDirection: "column",
              }}
            >
              <Typography variant="caption" color="text.secondary">
                Disconnected
              </Typography>
            </div>
          ) : webSocket.type === "binary" ? (
            messages[index] &&
            typeof messages[index] === "string" &&
            messages[index].startsWith("blob:") ? (
              <img
                src={messages[index]}
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
                fontSize: webSocket.fontSize || 14,
              }}
            >
              {typeof messages[index] === "string" ? messages[index] : ""}
            </Typography>
          )}

          {/* Text input for sending messages - only show when connected and sending is enabled */}
          {connectionStatus[index] === "connected" &&
            webSocket.enableSending !== false && (
              <Box sx={{ marginTop: 2, display: "flex", gap: 1 }}>
                <TextField
                  label="Send message"
                  value={inputTexts[index] || ""}
                  onChange={(e) => handleInputChange(index, e.target.value)}
                  onKeyPress={(e) => handleKeyPress(index, e)}
                  variant="outlined"
                  size="small"
                  fullWidth
                  placeholder="Type a message..."
                />
                <Button
                  onClick={() => handleSend(index)}
                  variant="contained"
                  size="small"
                  disabled={!inputTexts[index] || !inputTexts[index].trim()}
                >
                  Send
                </Button>
              </Box>
            )}
        </Module>
      ))}

      <SettingsModal
        open={openModal}
        onClose={handleCloseModal}
        title="WebSocket Settings"
        actions={
          <>
            <DeleteButton
              onClick={handleDeleteSocket}
              tooltip={"Delete Websocket"}
            />
            <IButton
              onClick={handleReconnect}
              tooltip={"Reconnect WebSocket"}
              Icon={RestartAlt}
            />
            <IButton
              onClick={handleDisconnect}
              tooltip={"Disconnect WebSocket"}
              Icon={LinkOff}
            />
            <SaveButton
              onClick={handleSaveSettings}
              tooltip={"Apply Websocket Settings"}
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
          InputProps={{
            startAdornment: (
              <span style={{ color: "#666", marginRight: "8px" }}></span>
            ),
          }}
        />
        <FormControl fullWidth sx={{ marginBottom: 2 }}>
          <InputLabel id="websocket-type-select-label">Type</InputLabel>
          <Select
            labelId="websocket-type-select-label"
            id="websocket-type-select"
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
    </Container>
  );
}
