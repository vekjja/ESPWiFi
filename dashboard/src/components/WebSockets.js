import React, { useState, useEffect } from "react";
import {
  Container,
  Typography,
  Card,
  CardContent,
  CardActions,
  IconButton,
  Modal,
  Box,
  Button,
  TextField,
} from "@mui/material";
import SettingsIcon from "@mui/icons-material/Settings";

export default function WebSockets({ config, saveConfig }) {
  const [webSockets, setWebSockets] = useState([]);
  const [imageURLs, setImageURLs] = useState([]);
  const [openModal, setOpenModal] = useState(false);
  const [selectedWebSocket, setSelectedWebSocket] = useState(null);
  const [editedURL, setEditedURL] = useState("");
  const [editedName, setEditedName] = useState("");

  useEffect(() => {
    if (config && config.webSockets) {
      setWebSockets(config.webSockets);
    }
  }, [config]);

  useEffect(() => {
    const sockets = webSockets.map((webSocket, index) => {
      const ws = new WebSocket(webSocket.url);
      ws.binaryType = "arraybuffer";

      ws.onmessage = (event) => {
        const blob = new Blob([event.data], { type: "image/jpeg" });
        const objectURL = URL.createObjectURL(blob);
        setImageURLs((prev) => {
          const updated = [...prev];
          updated[index] = objectURL;
          return updated;
        });
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
        ? { ...selectedWebSocket, url: editedURL, name: editedName }
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
    }
  }, [selectedWebSocket]);

  return (
    <Container sx={{ marginTop: 2 }}>
      {webSockets.map((webSocket, index) => (
        <Card key={index} sx={{ marginBottom: 2 }}>
          <CardContent>
            <Typography variant="body1">
              {webSocket.name || "Unnamed"}
            </Typography>
            <img
              src={imageURLs[index]}
              alt="Camera Stream"
              style={{ width: "100%", marginTop: "10px" }}
            />
          </CardContent>
          <CardActions sx={{ justifyContent: "flex-start" }}>
            <IconButton onClick={() => handleSettingsClick(webSocket)}>
              <SettingsIcon />
            </IconButton>
          </CardActions>
        </Card>
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
          <Button
            variant="contained"
            color="primary"
            onClick={handleSaveSettings}
            sx={{ mt: 2 }}
          >
            Save
          </Button>
          <Button
            variant="outlined"
            onClick={handleCloseModal}
            sx={{ mt: 2, ml: 2 }}
          >
            Cancel
          </Button>
          <Button
            variant="contained"
            color="error"
            onClick={handleDeleteSocket}
            sx={{ mt: 2, ml: 2 }}
          >
            Delete
          </Button>
        </Box>
      </Modal>
    </Container>
  );
}
