import React, { useState } from "react";
import { Fab, Menu, MenuItem, ListItemIcon, ListItemText } from "@mui/material";
import AddIcon from "@mui/icons-material/Add";
import PinIcon from "@mui/icons-material/Input";
import WebSocketIcon from "@mui/icons-material/Wifi";
import PinSettingsModal from "./PinSettingsModal";
import WebSocketSettingsModal from "./WebSocketSettingsModal";

export default function AddModule({ config, saveConfig }) {
  const [anchorEl, setAnchorEl] = useState(null);
  const [pinModalOpen, setPinModalOpen] = useState(false);
  const [webSocketModalOpen, setWebSocketModalOpen] = useState(false);
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

  const handleOpenMenu = (event) => {
    setAnchorEl(event.currentTarget);
  };

  const handleCloseMenu = () => {
    setAnchorEl(null);
  };

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
    handleCloseMenu();
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
    handleCloseMenu();
  };

  const handleClosePinModal = () => {
    setPinModalOpen(false);
  };

  const handleCloseWebSocketModal = () => {
    setWebSocketModalOpen(false);
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

    // Handle new unified modules format
    if (config.modules && Array.isArray(config.modules)) {
      const updatedModules = [...config.modules, newPin];
      saveConfig({ ...config, modules: updatedModules });
    } else {
      // Convert old format to new format
      const existingModules = [];

      // Convert existing pins
      const pins = config.pins || [];
      if (Array.isArray(pins)) {
        pins.forEach((pin, index) => {
          existingModules.push({
            ...pin,
            type: "pin",
            key: index,
          });
        });
      } else if (typeof pins === "object") {
        Object.entries(pins).forEach(([pinNum, pinData], index) => {
          existingModules.push({
            ...pinData,
            type: "pin",
            number: parseInt(pinNum, 10),
            key: index,
          });
        });
      }

      // Convert existing webSockets
      const webSockets = config.webSockets || [];
      webSockets.forEach((webSocket, index) => {
        existingModules.push({
          ...webSocket,
          type: "webSocket",
          key: existingModules.length + index,
        });
      });

      // Add new pin
      existingModules.push(newPin);
      saveConfig({ ...config, modules: existingModules });
    }

    handleClosePinModal();
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

    // Handle new unified modules format
    if (config.modules && Array.isArray(config.modules)) {
      const updatedModules = [...config.modules, newWebSocket];
      saveConfig({ ...config, modules: updatedModules });
    } else {
      // Convert old format to new format
      const existingModules = [];

      // Convert existing pins
      const pins = config.pins || [];
      if (Array.isArray(pins)) {
        pins.forEach((pin, index) => {
          existingModules.push({
            ...pin,
            type: "pin",
            key: index,
          });
        });
      } else if (typeof pins === "object") {
        Object.entries(pins).forEach(([pinNum, pinData], index) => {
          existingModules.push({
            ...pinData,
            type: "pin",
            number: parseInt(pinNum, 10),
            key: index,
          });
        });
      }

      // Convert existing webSockets
      const webSockets = config.webSockets || [];
      webSockets.forEach((webSocket, index) => {
        existingModules.push({
          ...webSocket,
          type: "webSocket",
          key: existingModules.length + index,
        });
      });

      // Add new webSocket
      existingModules.push(newWebSocket);
      saveConfig({ ...config, modules: existingModules });
    }

    handleCloseWebSocketModal();
  };

  return (
    <>
      <Fab
        color="primary"
        aria-label="add module"
        onClick={handleOpenMenu}
        sx={{
          position: "fixed",
          top: "20px",
          right: "20px",
          zIndex: 1001,
        }}
      >
        <AddIcon />
      </Fab>

      <Menu
        anchorEl={anchorEl}
        open={Boolean(anchorEl)}
        onClose={handleCloseMenu}
        anchorOrigin={{
          vertical: "bottom",
          horizontal: "right",
        }}
        transformOrigin={{
          vertical: "top",
          horizontal: "right",
        }}
      >
        <MenuItem onClick={handleOpenPinModal}>
          <ListItemIcon>
            <PinIcon fontSize="small" />
          </ListItemIcon>
          <ListItemText>Add Pin</ListItemText>
        </MenuItem>
        <MenuItem onClick={handleOpenWebSocketModal}>
          <ListItemIcon>
            <WebSocketIcon fontSize="small" />
          </ListItemIcon>
          <ListItemText>Add WebSocket</ListItemText>
        </MenuItem>
      </Menu>

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
    </>
  );
}
