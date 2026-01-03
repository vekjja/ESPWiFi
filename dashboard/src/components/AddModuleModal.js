import React, { useState } from "react";
import {
  Button,
  Divider,
  List,
  ListItem,
  ListItemIcon,
  ListItemText,
  ListItemButton,
} from "@mui/material";
import SettingsModal from "./SettingsModal";
import {
  Input as PinIcon,
  Wifi as WebSocketIcon,
  CameraAlt as CameraAltIcon,
  Add as AddIcon,
  Settings as SettingsIcon,
} from "@mui/icons-material";
import PinSettingsModal from "./PinSettingsModal";
import WebSocketSettingsModal from "./WebSocketSettingsModal";
import CameraSettingsModal from "./CameraSettingsModal";

export default function AddModuleModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
  missingSettingsButtons = [],
  onAddSettingsButton,
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
    url: "/ws/camera",
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
      url: "/ws/camera",
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
    // Validate required fields
    if (!pinData.pinNumber || pinData.pinNumber === "") {
      alert("Please enter a pin number");
      return;
    }

    const pinNumber = parseInt(pinData.pinNumber, 10);
    if (isNaN(pinNumber) || pinNumber < 0) {
      alert("Please enter a valid pin number");
      return;
    }

    const existingModules = config.modules || [];
    const newPin = {
      type: "pin",
      number: pinNumber,
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
    const configToSave = { ...config, modules: updatedModules };
    saveConfig(configToSave); // Update local config
    saveConfigToDevice(configToSave); // Save to device
    handleClosePinModal();
    onClose(); // Close the main Add Module modal
  };

  const handleSaveWebSocket = () => {
    // Validate required fields
    if (!webSocketData.url || webSocketData.url.trim() === "") {
      alert("Please enter a WebSocket URL");
      return;
    }

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
    const configToSave = { ...config, modules: updatedModules };
    saveConfig(configToSave); // Update local config
    saveConfigToDevice(configToSave); // Save to device
    handleCloseWebSocketModal();
    onClose(); // Close the main Add Module modal
  };

  const handleSaveCamera = () => {
    // Validate required fields
    if (!cameraData.url || cameraData.url.trim() === "") {
      alert("Please enter a camera URL");
      return;
    }

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
    const configToSave = { ...config, modules: updatedModules };
    saveConfig(configToSave); // Update local config
    saveConfigToDevice(configToSave); // Save to device
    handleCloseCameraModal();
    onClose(); // Close the main Add Module modal
  };

  return (
    <>
      <SettingsModal
        open={open}
        onClose={onClose}
        title={
          <span
            style={{
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              gap: "8px",
              width: "100%",
            }}
          >
            <AddIcon color="primary" />
            Add Module
          </span>
        }
        actions={
          <Button onClick={onClose} color="inherit">
            Cancel
          </Button>
        }
      >
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

          {Array.isArray(missingSettingsButtons) &&
            missingSettingsButtons.length > 0 && (
              <>
                <Divider sx={{ my: 1 }} />
                <ListItem disablePadding>
                  <ListItemButton disabled>
                    <ListItemIcon>
                      <SettingsIcon />
                    </ListItemIcon>
                    <ListItemText
                      primary="Add Settings Button"
                      secondary="Add a button back to the top bar"
                    />
                  </ListItemButton>
                </ListItem>

                {missingSettingsButtons.map((b) => (
                  <ListItem key={b.id} disablePadding>
                    <ListItemButton
                      onClick={() => {
                        if (onAddSettingsButton) onAddSettingsButton(b.id);
                      }}
                    >
                      <ListItemIcon>
                        <AddIcon />
                      </ListItemIcon>
                      <ListItemText primary={b.label || b.id} />
                    </ListItemButton>
                  </ListItem>
                ))}
              </>
            )}
        </List>
      </SettingsModal>

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
        config={config}
      />
    </>
  );
}
