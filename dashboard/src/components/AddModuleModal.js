import React, { useState } from "react";
import {
  Button,
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  List,
  ListItem,
  ListItemIcon,
  ListItemText,
  ListItemButton,
} from "@mui/material";
import {
  Input as PinIcon,
  Wifi as WebSocketIcon,
  CameraAlt as CameraAltIcon,
} from "@mui/icons-material";
import PinSettingsModal from "./PinSettingsModal";
import WebSocketSettingsModal from "./WebSocketSettingsModal";
import CameraSettingsModal from "./CameraSettingsModal";

export default function AddModuleModal({
  config,
  saveConfig,
  open = false,
  onClose,
}) {
  const [pinModalOpen, setPinModalOpen] = useState(false);
  const [webSocketModalOpen, setWebSocketModalOpen] = useState(false);
  const [cameraModalOpen, setCameraModalOpen] = useState(false);

  const [pinData, setPinData] = useState({
    name: "",
    pinNumber: "",
    mode: "out",
    inverted: false,
    remoteURL: "",
    dutyMin: 0,
    dutyMax: 255,
  });

  const [webSocketData, setWebSocketData] = useState({
    name: "",
    url: "",
    payload: "text",
    fontSize: 14,
    enableSending: true,
  });

  const [cameraData, setCameraData] = useState({
    name: "",
    url: "/camera",
    frameRate: 10,
  });

  const handleOpenPinModal = () => {
    setPinData({
      name: "",
      pinNumber: "",
      mode: "out",
      inverted: false,
      remoteURL: "",
      dutyMin: 0,
      dutyMax: 255,
    });
    setPinModalOpen(true);
  };

  const handleOpenWebSocketModal = () => {
    setWebSocketData({
      name: "",
      url: "",
      payload: "text",
      fontSize: 14,
      enableSending: true,
    });
    setWebSocketModalOpen(true);
  };

  const handleOpenCameraModal = () => {
    setCameraData({
      name: "",
      url: "/camera",
      frameRate: 10,
    });
    setCameraModalOpen(true);
  };

  const handleClosePinModal = () => {
    setPinModalOpen(false);
  };

  const handleCloseWebSocketModal = () => {
    setWebSocketModalOpen(false);
  };

  const handleCloseCameraModal = () => {
    setCameraModalOpen(false);
  };

  // Helper function to generate unique key
  const generateUniqueKey = (existingModules) => {
    if (!existingModules || existingModules.length === 0) {
      return 0;
    }
    const maxKey = Math.max(
      ...existingModules.map((m) => (typeof m.key === "number" ? m.key : -1))
    );
    return maxKey + 1;
  };

  const handlePinDataChange = (changes) => {
    setPinData((prev) => ({ ...prev, ...changes }));
  };

  const handleWebSocketDataChange = (changes) => {
    setWebSocketData((prev) => ({ ...prev, ...changes }));
  };

  const handleCameraDataChange = (newData) => {
    setCameraData(newData);
  };

  const handleSavePin = () => {
    if (!pinData.pinNumber || pinData.pinNumber === "") return;

    const existingModules = config.modules || [];
    const newPin = {
      type: "pin",
      number: parseInt(pinData.pinNumber, 10),
      state: "low",
      name: pinData.name || `GPIO${pinData.pinNumber}`,
      mode: pinData.mode,
      inverted: pinData.inverted,
      remoteURL: pinData.remoteURL,
      dutyMin: pinData.dutyMin,
      dutyMax: pinData.dutyMax,
      key: generateUniqueKey(existingModules),
    };

    const updatedModules = [...existingModules, newPin];
    saveConfig({ ...config, modules: updatedModules });
    handleClosePinModal();
    onClose(); // Close the main Add Module modal
  };

  const handleSaveWebSocket = () => {
    if (!webSocketData.url || webSocketData.url.trim() === "") return;

    const existingModules = config.modules || [];
    const newWebSocket = {
      type: "webSocket",
      url: webSocketData.url.trim(),
      name: webSocketData.name || "Unnamed",
      payload: webSocketData.payload,
      fontSize: webSocketData.fontSize,
      enableSending: webSocketData.enableSending,
      connectionState: "disconnected",
      key: generateUniqueKey(existingModules),
    };

    const updatedModules = [...existingModules, newWebSocket];
    saveConfig({ ...config, modules: updatedModules });
    handleCloseWebSocketModal();
    onClose(); // Close the main Add Module modal
  };

  const handleSaveCamera = () => {
    if (!cameraData.url || cameraData.url.trim() === "") return;

    const existingModules = config.modules || [];
    const newCamera = {
      type: "camera",
      url: cameraData.url.trim(),
      name:
        cameraData.name ||
        `Camera ${
          existingModules.filter((m) => m.type === "camera").length + 1
        }`,
      frameRate: cameraData.frameRate,
      key: generateUniqueKey(existingModules),
    };

    const updatedModules = [...existingModules, newCamera];
    saveConfig({ ...config, modules: updatedModules });
    handleCloseCameraModal();
    onClose(); // Close the main Add Module modal
  };

  return (
    <>
      <Dialog open={open} onClose={onClose} maxWidth="sm" fullWidth>
        <DialogTitle>Add Module</DialogTitle>
        <DialogContent>
          {/* <Typography variant="body1" sx={{ mb: 2 }}>
            Add a module:
          </Typography> */}
          <List>
            <ListItem disablePadding>
              <ListItemButton onClick={handleOpenPinModal}>
                <ListItemIcon>
                  <PinIcon />
                </ListItemIcon>
                <ListItemText primary="Pin" secondary="Control GPIO pins" />
              </ListItemButton>
            </ListItem>
            <ListItem disablePadding>
              <ListItemButton onClick={handleOpenWebSocketModal}>
                <ListItemIcon>
                  <WebSocketIcon />
                </ListItemIcon>
                <ListItemText
                  primary="WebSocket"
                  secondary="Connect to WebSocket streams"
                />
              </ListItemButton>
            </ListItem>
            <ListItem disablePadding>
              <ListItemButton onClick={handleOpenCameraModal}>
                <ListItemIcon>
                  <CameraAltIcon />
                </ListItemIcon>
                <ListItemText primary="Camera" secondary="Add camera module" />
              </ListItemButton>
            </ListItem>
          </List>
        </DialogContent>
        <DialogActions>
          <Button onClick={onClose} color="inherit">
            Cancel
          </Button>
        </DialogActions>
      </Dialog>

      <PinSettingsModal
        open={pinModalOpen}
        onClose={handleClosePinModal}
        onSave={handleSavePin}
        pinData={pinData}
        onPinDataChange={handlePinDataChange}
      />

      <WebSocketSettingsModal
        open={webSocketModalOpen}
        onClose={handleCloseWebSocketModal}
        onSave={handleSaveWebSocket}
        websocketData={webSocketData}
        onWebSocketDataChange={handleWebSocketDataChange}
      />

      <CameraSettingsModal
        open={cameraModalOpen}
        onClose={handleCloseCameraModal}
        onSave={handleSaveCamera}
        cameraData={cameraData}
        onCameraDataChange={handleCameraDataChange}
      />
    </>
  );
}
