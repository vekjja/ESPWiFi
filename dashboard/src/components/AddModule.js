import React, { useState } from "react";
import {
  Fab,
  Menu,
  MenuItem,
  ListItemIcon,
  ListItemText,
  Dialog,
  DialogTitle,
  DialogContent,
  Slider,
  Typography,
  Box,
  Button,
  TextField,
} from "@mui/material";
import AddIcon from "@mui/icons-material/Add";
import PinIcon from "@mui/icons-material/Input";
import WebSocketIcon from "@mui/icons-material/Wifi";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import PinSettingsModal from "./PinSettingsModal";
import WebSocketSettingsModal from "./WebSocketSettingsModal";

export default function AddModule({
  config,
  saveConfig,
  open = false,
  onClose,
  standalone = false, // New prop to indicate if this is a standalone component
}) {
  const [anchorEl, setAnchorEl] = useState(null);
  const buttonRef = React.useRef(null);
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

  const handleOpenMenu = (event) => {
    // Handle case where event is provided (from button click) or not (from useEffect)
    if (event && event.currentTarget) {
      setAnchorEl(event.currentTarget);
    } else {
      // For programmatic opening, use the button ref
      setAnchorEl(buttonRef.current);
    }
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

  const handleOpenCameraModal = () => {
    setCameraData({
      name: "",
      url: "/camera",
      frameRate: 10,
    });
    setCameraModalOpen(true);
    handleCloseMenu();
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
  };

  // Handle external open prop
  React.useEffect(() => {
    if (open) {
      handleOpenMenu();
    }
  }, [open]);

  return (
    <>
      {/* Only render the Fab button when in standalone mode */}
      {standalone && (
        <Fab
          ref={buttonRef}
          size="small"
          color="primary"
          aria-label="add module"
          onClick={handleOpenMenu}
          sx={
            {
              // Remove fixed positioning - will be handled by SettingsButtonBar
            }
          }
        >
          <AddIcon />
        </Fab>
      )}

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
        <MenuItem onClick={handleOpenCameraModal}>
          <ListItemIcon>
            <CameraAltIcon fontSize="small" />
          </ListItemIcon>
          <ListItemText>Add Camera</ListItemText>
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

      <Dialog
        open={cameraModalOpen}
        onClose={handleCloseCameraModal}
        maxWidth="sm"
        fullWidth
      >
        <DialogTitle>Add Camera Module</DialogTitle>
        <DialogContent>
          <Box sx={{ marginTop: 2 }}>
            <TextField
              fullWidth
              label="Camera Name (optional)"
              value={cameraData.name}
              onChange={(e) =>
                setCameraData({ ...cameraData, name: e.target.value })
              }
              sx={{ marginBottom: 3 }}
            />

            <TextField
              fullWidth
              label="Camera URL"
              value={cameraData.url}
              onChange={(e) =>
                setCameraData({ ...cameraData, url: e.target.value })
              }
              placeholder="/camera"
              helperText="WebSocket endpoint for camera stream"
              sx={{ marginBottom: 3 }}
            />

            <Typography gutterBottom>
              Frame Rate: {cameraData.frameRate} FPS
            </Typography>
            <Slider
              value={cameraData.frameRate}
              onChange={(e, newValue) =>
                setCameraData({ ...cameraData, frameRate: newValue })
              }
              min={1}
              max={30}
              step={1}
              marks={[
                { value: 1, label: "1" },
                { value: 5, label: "5" },
                { value: 10, label: "10" },
                { value: 15, label: "15" },
                { value: 20, label: "20" },
                { value: 25, label: "25" },
                { value: 30, label: "30" },
              ]}
              valueLabelDisplay="auto"
              sx={{
                color: "primary.main",
                "& .MuiSlider-thumb": {
                  backgroundColor: "primary.main",
                },
                "& .MuiSlider-track": {
                  backgroundColor: "primary.main",
                },
                "& .MuiSlider-rail": {
                  backgroundColor: "rgba(255, 255, 255, 0.2)",
                },
                "& .MuiSlider-mark": {
                  backgroundColor: "primary.main",
                },
                "& .MuiSlider-markLabel": {
                  color: "primary.main",
                },
              }}
            />
          </Box>

          <Box
            sx={{
              marginTop: 3,
              padding: 2,
              backgroundColor: "rgba(71, 255, 240, 0.1)",
              borderRadius: 1,
            }}
          >
            <Typography variant="body2" color="primary.main">
              📷 Camera module will be added to the dashboard for live
              streaming.
            </Typography>
          </Box>

          <Box
            sx={{
              marginTop: 2,
              display: "flex",
              justifyContent: "flex-end",
              gap: 2,
            }}
          >
            <Button onClick={handleCloseCameraModal} color="inherit">
              Cancel
            </Button>
            <Button
              onClick={handleSaveCamera}
              variant="contained"
              color="primary"
            >
              Add Camera
            </Button>
          </Box>
        </DialogContent>
      </Dialog>
    </>
  );
}
