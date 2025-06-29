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

  const pinOptions = Array.from({ length: 22 }, (_, i) => i).filter(
    (pin) =>
      !Object.keys(config?.pins || {})
        .map(Number)
        .includes(pin)
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
    if (selectedTab === 1) {
      // Add WebSocket
      const newWebSocket = {
        url: webSocketURL,
        name: "Unnamed",
        type: "text",
        fontSize: 14,
        enableSending: true,
        connectionState: "disconnected",
      };
      const updatedWebSockets = [...(config.webSockets || []), newWebSocket];
      saveConfig({ ...config, webSockets: updatedWebSockets });
    } else {
      // Add Pin
      const newPin = {
        state: "low",
        name: `GPIO${selectedPinNum}`,
        mode: "out",
      };

      const updatedPins = {
        ...(config.pins || {}),
        [selectedPinNum]: newPin,
      };
      saveConfig({ ...config, pins: updatedPins });
      setSelectedPinNum(""); // Clear the selected pin to ensure UI updates
    }
    handleCloseModal();
  };

  const handleTabChange = (event, newValue) => {
    setSelectedTab(newValue);
    // Set default value when switching to WebSocket tab
    if (newValue === 1 && !webSocketURL) {
      setWebSocketURL("ws://");
    }
  };

  const handleWebSocketURLChange = (e) => {
    let value = e.target.value;

    // Allow relative URLs (starting with /) or absolute URLs (starting with ws:// or wss://)
    // Don't automatically prepend ws:// for relative paths
    if (value && !value.match(/^(ws|wss):\/\//) && !value.startsWith("/")) {
      // Only prepend ws:// if it's not a relative path and doesn't have a protocol
      value = `ws://${value}`;
    }

    setWebSocketURL(value);
  };

  return (
    <>
      <Tooltip title={"Add Module"}>
        <Fab
          size="small" // Match the size of the SettingsIcon Fab
          color="primary"
          onClick={handleOpenModal}
          sx={{
            position: "fixed",
            top: "20px",
            right: "20px",
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
