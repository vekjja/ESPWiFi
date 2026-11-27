import React, { useState } from "react";
import {
  Button,
  Divider,
  List,
  ListItem,
  ListItemIcon,
  ListItemText,
  ListItemButton,
  Typography,
} from "@mui/material";
import SettingsModal from "../common/SettingsModal";
import PinIcon from "@mui/icons-material/Input";
import WebSocketIcon from "@mui/icons-material/Wifi";
import LibraryMusicIcon from "@mui/icons-material/LibraryMusic";
import { SECTIONS } from "../common/SectionTitle";
import AddIcon from "@mui/icons-material/Add";
import SettingsIcon from "@mui/icons-material/Settings";
import PinSettingsModal from "./pin/PinSettingsModal";
import WebSocketSettingsModal from "./websocket/WebSocketSettingsModal";
import CameraSettingsModal from "./camera/CameraSettingsModal";
import MusicPlayerSettingsModal from "./music/MusicPlayerSettingsModal";

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
  const [musicPlayerModalOpen, setMusicPlayerModalOpen] = useState(false);

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
  });

  const [musicPlayerData, setMusicPlayerData] = useState({
    musicDir: "/music",
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
    });
    setCameraModalOpen(true);
  };

  const handleOpenMusicPlayerModal = () => {
    setMusicPlayerData({
      name: "",
      musicDir: "/music",
    });
    setMusicPlayerModalOpen(true);
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

  const handleCloseMusicPlayerModal = () => {
    setMusicPlayerModalOpen(false);
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

  const handleMusicPlayerDataChange = (changes) => {
    setMusicPlayerData((prev) => ({ ...prev, ...changes }));
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
    const configToSave = { modules: updatedModules };
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
    const configToSave = { modules: updatedModules };
    saveConfig(configToSave); // Update local config
    saveConfigToDevice(configToSave); // Save to device
    handleCloseWebSocketModal();
    onClose(); // Close the main Add Module modal
  };

  const handleSaveCamera = () => {
    const existingModules = config.modules || [];
    const newCamera = {
      type: "camera",
      name: cameraData.name || "Camera",
      key: generateUniqueKey(existingModules),
    };

    const updatedModules = [...existingModules, newCamera];
    const configToSave = { modules: updatedModules };
    saveConfig(configToSave);
    saveConfigToDevice(configToSave);
    handleCloseCameraModal();
    onClose();
  };

  const handleSaveMusicPlayer = () => {
    // Validate required fields
    if (!musicPlayerData.musicDir || musicPlayerData.musicDir.trim() === "") {
      alert("Please enter a music directory path");
      return;
    }

    const existingModules = config.modules || [];
    const newMusicPlayer = {
      type: "musicPlayer",
      name: musicPlayerData.name || "Music Player",
      musicDir: musicPlayerData.musicDir.trim().startsWith("/")
        ? musicPlayerData.musicDir.trim()
        : "/" + musicPlayerData.musicDir.trim(),
      key: generateUniqueKey(existingModules),
    };

    const updatedModules = [...existingModules, newMusicPlayer];
    const configToSave = { modules: updatedModules };
    saveConfig(configToSave);
    saveConfigToDevice(configToSave);
    handleCloseMusicPlayerModal();
    onClose();
  };

  const handleAddSystemInfo = (info) => {
    const existingModules = config?.modules || [];
    if (
      existingModules.some(
        (mod) => mod.type === "systemInfo" && mod.info === info
      )
    ) {
      return;
    }
    const newModule = {
      type: "systemInfo",
      info,
      name: SECTIONS[info]?.title || "System Info",
      key: generateUniqueKey(existingModules),
    };
    const updatedModules = [...existingModules, newModule];
    const configToSave = { modules: updatedModules };
    saveConfig(configToSave);
    saveConfigToDevice(configToSave);
    onClose();
  };

  const hasSystemInfo = (info) =>
    (config?.modules || []).some(
      (mod) => mod.type === "systemInfo" && mod.info === info
    );

  const CameraIcon = SECTIONS.camera.icon;
  const MemoryIcon = SECTIONS.memory.icon;
  const NetworkIcon = SECTIONS.network.icon;
  const StorageIcon = SECTIONS.storage.icon;

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
                <CameraIcon />
              </ListItemIcon>
              <ListItemText
                primary={SECTIONS.camera.title}
                secondary="Add camera module"
              />
            </ListItemButton>
          </ListItem>

          <ListItem disablePadding>
            <ListItemButton onClick={handleOpenMusicPlayerModal}>
              <ListItemIcon>
                <LibraryMusicIcon />
              </ListItemIcon>
              <ListItemText
                primary="Music Player"
                secondary="Play music from SD card"
              />
            </ListItemButton>
          </ListItem>

          <Divider sx={{ my: 1 }} />
          <ListItem>
            <ListItemText
              primary="System Info"
              secondary="Live memory, network, and storage"
            />
          </ListItem>
          <ListItem disablePadding>
            <ListItemButton
              onClick={() => handleAddSystemInfo("memory")}
              disabled={hasSystemInfo("memory")}
            >
              <ListItemIcon>
                <MemoryIcon />
              </ListItemIcon>
              <ListItemText
                primary={SECTIONS.memory.title}
                secondary="Free, used, and total heap"
              />
            </ListItemButton>
          </ListItem>
          <ListItem disablePadding>
            <ListItemButton
              onClick={() => handleAddSystemInfo("network")}
              disabled={hasSystemInfo("network")}
            >
              <ListItemIcon>
                <NetworkIcon />
              </ListItemIcon>
              <ListItemText
                primary={SECTIONS.network.title}
                secondary="Name, hostname, IP, and MAC"
              />
            </ListItemButton>
          </ListItem>
          <ListItem disablePadding>
            <ListItemButton
              onClick={() => handleAddSystemInfo("storage")}
              disabled={hasSystemInfo("storage")}
            >
              <ListItemIcon>
                <StorageIcon />
              </ListItemIcon>
              <ListItemText
                primary={SECTIONS.storage.title}
                secondary="LittleFS and SD card usage"
              />
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
        <Typography variant="body2" color="text.secondary" sx={{ px: 2, pt: 1 }}>
          Modules are saved in this browser for the current device. Clearing
          the browser cache resets that layout.
        </Typography>
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
        onCameraDataChange={handleCameraDataChange}
        saveConfigToDevice={saveConfigToDevice}
        onClose={handleCloseCameraModal}
        onSave={handleSaveCamera}
        cameraData={cameraData}
        open={cameraModalOpen}
        config={config}
      />

      <MusicPlayerSettingsModal
        open={musicPlayerModalOpen}
        onClose={handleCloseMusicPlayerModal}
        onSave={handleSaveMusicPlayer}
        musicPlayerData={musicPlayerData}
        onMusicPlayerDataChange={handleMusicPlayerDataChange}
      />
    </>
  );
}
