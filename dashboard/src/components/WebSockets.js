import React, { useState, useEffect } from "react";
import {
  Container,
  Typography,
  Modal,
  Box,
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
} from "@mui/material";
import DeleteButton from "./DeleteButton";
import SaveButton from "./SaveButton";
import Module from "./Module";

export default function WebSockets({ config, saveConfig }) {
  const [webSockets, setWebSockets] = useState([]);
  const [messages, setMessages] = useState([]);
  const [openModal, setOpenModal] = useState(false);
  const [selectedWebSocket, setSelectedWebSocket] = useState(null);
  const [editedURL, setEditedURL] = useState("");
  const [editedName, setEditedName] = useState("");
  const [editedType, setEditedType] = useState("binary");
  const [editedFontSize, setEditedFontSize] = useState(14);

  useEffect(() => {
    if (config && config.webSockets) {
      setWebSockets(
        config.webSockets.map((ws) => ({
          ...ws,
          type: ws.type || "binary",
          fontSize: ws.fontSize || 14,
        }))
      );
    }
  }, [config]);

  useEffect(() => {
    const sockets = webSockets.map((webSocket, index) => {
      const ws = new WebSocket(webSocket.url);
      if (webSocket.type === "binary") {
        ws.binaryType = "arraybuffer";
      }

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
      };

      return ws;
    });

    return () => {
      sockets.forEach((ws) => ws.close());
    };
  }, [webSockets]);

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
      setEditedType(selectedWebSocket.type || "binary");
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
          {webSocket.type === "binary" ? (
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

      <Modal open={openModal} onClose={handleCloseModal}>
        <Box
          sx={{
            position: "absolute",
            top: "50%",
            left: "50%",
            transform: "translate(-50%, -50%)",
            width: 300,
            bgcolor: "background.paper",
            boxShadow: 24,
            p: 4,
            borderRadius: 2,
          }}
        >
          <Typography variant="h6" gutterBottom>
            WebSocket Settings
          </Typography>
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
            inputProps={{ min: 8, max: 48 }}
          />
          <DeleteButton
            onClick={handleDeleteSocket}
            tooltip={"Delete Websocket"}
          />
          <SaveButton
            onClick={handleSaveSettings}
            tooltip={"Save Websocket Settings"}
          />
        </Box>
      </Modal>
    </Container>
  );
}
