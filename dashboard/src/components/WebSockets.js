import React, { useState, useEffect, useRef } from "react";
import {
  Container,
  Typography,
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  CircularProgress,
} from "@mui/material";
import DeleteButton from "./DeleteButton";
import SaveButton from "./SaveButton";
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

  // Use refs to store WebSocket instances for proper cleanup
  const socketRefs = useRef({});

  useEffect(() => {
    if (config && config.webSockets) {
      setWebSockets(
        config.webSockets.map((ws) => ({
          ...ws,
          type: ws.type || "text",
          fontSize: ws.fontSize || 14,
        }))
      );
    }
  }, [config]);

  useEffect(() => {
    // Clean up existing connections first
    Object.values(socketRefs.current).forEach((ws) => {
      if (ws && ws.readyState !== WebSocket.CLOSED) {
        ws.close();
      }
    });
    socketRefs.current = {};

    // Create new connections only if webSockets array has changed
    webSockets.forEach((webSocket, index) => {
      // Set loading state for this socket
      setConnectionStatus((prev) => ({
        ...prev,
        [index]: "connecting",
      }));

      const ws = new WebSocket(webSocket.url);
      if (webSocket.type === "binary") {
        ws.binaryType = "arraybuffer";
      }

      // Store the WebSocket instance in refs for cleanup
      socketRefs.current[index] = ws;

      ws.onopen = () => {
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "connected",
        }));
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
        console.error("WebSocket error:", error);
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "error",
        }));
      };

      ws.onclose = () => {
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "📶",
        }));
        // Remove from refs when closed
        delete socketRefs.current[index];
      };
    });

    // Cleanup function
    return () => {
      Object.values(socketRefs.current).forEach((ws) => {
        if (ws && ws.readyState !== WebSocket.CLOSED) {
          ws.close();
        }
      });
      socketRefs.current = {};
    };
  }, [webSockets]); // Only recreate when webSockets array changes

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
    setWebSockets(updatedWebSockets);

    // Update the config and save it
    const updatedConfig = { ...config, webSockets: updatedWebSockets };
    saveConfig(updatedConfig);

    handleCloseModal();
  };

  const handleSaveSettings = () => {
    const updatedWebSockets = webSockets.map((ws) =>
      ws === selectedWebSocket
        ? {
            ...selectedWebSocket,
            url: editedURL,
            name: editedName,
            type: editedType,
            fontSize: Number(editedFontSize),
          }
        : ws
    );
    setWebSockets(updatedWebSockets);

    // Update the config and save it
    const updatedConfig = { ...config, webSockets: updatedWebSockets };
    saveConfig(updatedConfig);

    handleCloseModal();
  };

  useEffect(() => {
    if (selectedWebSocket) {
      setEditedURL(selectedWebSocket.url);
      setEditedName(selectedWebSocket.name || "");
      setEditedType(selectedWebSocket.type || "text");
      setEditedFontSize(selectedWebSocket.fontSize || 14);
    }
  }, [selectedWebSocket]);

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
          ) : connectionStatus[index] === "disconnected" ? (
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
              {messages[index]}
            </Typography>
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
            <SaveButton
              onClick={handleSaveSettings}
              tooltip={"Save Websocket Settings"}
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
      </SettingsModal>
    </Container>
  );
}
