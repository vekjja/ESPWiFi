import React, { useState } from "react";
import {
  TextField,
  FormControl,
  InputLabel,
  Tooltip,
  Select,
  MenuItem,
  Fab,
  Tabs,
  Tab,
} from "@mui/material";
import IButton from "./IButton";
import AddIcon from "@mui/icons-material/Add";
import AddTaskIcon from "@mui/icons-material/AddTask";
import SettingsModal from "./SettingsModal";

export default function AddButton({ config, saveConfig }) {
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [selectedTab, setSelectedTab] = useState(0);
  const [selectedPinNum, setSelectedPinNum] = useState("");
  const [webSocketURL, setWebSocketURL] = useState("");

  // Get used pin numbers from the current config
  const getUsedPinNumbers = () => {
    const pins = config?.pins || [];
    if (Array.isArray(pins)) {
      // New array format
      return pins.map((pin) => pin.number);
    } else {
      // Old object format - convert to array of numbers
      return Object.keys(pins).map(Number);
    }
  };

  const usedPinNumbers = getUsedPinNumbers();
  const pinOptions = Array.from({ length: 22 }, (_, i) => i).filter(
    (pin) => !usedPinNumbers.includes(pin)
  );

  const handleOpenModal = () => {
    setSelectedPinNum("");
    setWebSocketURL("");
    setSelectedTab(0);
    setIsModalOpen(true);
  };

  const handleCloseModal = () => {
    setIsModalOpen(false);
  };

  const handleAdd = () => {
    // Helper function to generate unique ID
    const generateUniqueId = (existingModules) => {
      if (!existingModules || existingModules.length === 0) {
        return 0;
      }
      const maxId = Math.max(...existingModules.map((m) => m.id || 0));
      return maxId + 1;
    };

    if (selectedTab === 1) {
      // Add WebSocket module
      const existingModules = config.modules || [];
      const newWebSocket = {
        type: "webSocket",
        url: webSocketURL,
        name: "Unnamed",
        payload: "text",
        fontSize: 14,
        enableSending: true,
        connectionState: "disconnected",
        id: generateUniqueId(existingModules),
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
              id: index,
            });
          });
        } else if (typeof pins === "object") {
          Object.entries(pins).forEach(([pinNum, pinData], index) => {
            existingModules.push({
              ...pinData,
              type: "pin",
              number: parseInt(pinNum, 10),
              id: index,
            });
          });
        }

        // Convert existing webSockets
        const webSockets = config.webSockets || [];
        webSockets.forEach((webSocket, index) => {
          existingModules.push({
            ...webSocket,
            type: "webSocket",
            id: existingModules.length + index,
          });
        });

        // Add new webSocket
        existingModules.push(newWebSocket);
        saveConfig({ ...config, modules: existingModules });
      }
    } else {
      // Add Pin module
      const existingModules = config.modules || [];
      const newPin = {
        type: "pin",
        number: parseInt(selectedPinNum, 10),
        state: "low",
        name: `GPIO${selectedPinNum}`,
        mode: "out",
        id: generateUniqueId(existingModules),
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
              id: index,
            });
          });
        } else if (typeof pins === "object") {
          Object.entries(pins).forEach(([pinNum, pinData], index) => {
            existingModules.push({
              ...pinData,
              type: "pin",
              number: parseInt(pinNum, 10),
              id: index,
            });
          });
        }

        // Convert existing webSockets
        const webSockets = config.webSockets || [];
        webSockets.forEach((webSocket, index) => {
          existingModules.push({
            ...webSocket,
            type: "webSocket",
            id: existingModules.length + index,
          });
        });

        // Add new pin
        existingModules.push(newPin);
        saveConfig({ ...config, modules: existingModules });
      }

      setSelectedPinNum(""); // Clear the selected pin to ensure UI updates
    }
    handleCloseModal();
  };

  const handleTabChange = (event, newValue) => {
    setSelectedTab(newValue);
  };

  const handleWebSocketURLChange = (event) => {
    setWebSocketURL(event.target.value);
  };

  return (
    <>
      <Tooltip title="Add Module">
        <Fab
          size="small"
          color="primary"
          onClick={handleOpenModal}
          sx={{
            position: "fixed",
            top: "20px",
            right: "20px",
            zIndex: 1001,
          }}
        >
          <AddIcon />
        </Fab>
      </Tooltip>

      <SettingsModal
        open={isModalOpen}
        onClose={handleCloseModal}
        title="Add Module"
        actions={
          <>
            <IButton
              color="primary"
              Icon={AddTaskIcon}
              onClick={handleAdd}
              tooltip={"Add " + (selectedTab === 1 ? "WebSocket" : "Pin")}
              disabled={
                selectedTab === 1 ? !webSocketURL : selectedPinNum === ""
              }
            />
          </>
        }
      >
        <Tabs
          value={selectedTab}
          onChange={handleTabChange}
          indicatorColor="primary"
          textColor="primary"
          centered
        >
          <Tab label="Pin" />
          <Tab label="WebSocket" />
        </Tabs>

        {selectedTab === 1 ? (
          <TextField
            label="WebSocket URL"
            value={webSocketURL}
            onChange={handleWebSocketURLChange}
            placeholder="ws://localhost:8080"
            variant="outlined"
            fullWidth
            sx={{ marginTop: 2 }}
          />
        ) : (
          <FormControl fullWidth variant="outlined" sx={{ marginTop: 2 }}>
            <InputLabel id="pin-select-label">Pin</InputLabel>
            <Select
              labelId="pin-select-label"
              value={selectedPinNum}
              onChange={(e) => setSelectedPinNum(e.target.value)}
            >
              {pinOptions.map((pin) => (
                <MenuItem key={pin} value={pin}>
                  GPIO{pin}
                </MenuItem>
              ))}
            </Select>
          </FormControl>
        )}
      </SettingsModal>
    </>
  );
}
