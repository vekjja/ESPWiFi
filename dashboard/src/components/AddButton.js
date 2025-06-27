import React, { useState } from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  Fab,
  Tabs,
  Tab,
} from "@mui/material";
import IButton from "./IButton";
import AddIcon from "@mui/icons-material/Add";
import AddTaskIcon from "@mui/icons-material/AddTask";

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
      const updatedWebSockets = [
        ...(config.webSockets || []),
        { url: webSocketURL },
      ];
      saveConfig({ ...config, webSockets: updatedWebSockets });
    } else {
      const updatedPins = {
        ...config.pins,
        [selectedPinNum]: {
          state: "low",
          name: `GPIO${selectedPinNum}`,
          mode: "out",
          duty: 1860,
          cycle: 20000,
          hz: 50,
        },
      };
      saveConfig({ ...config, pins: updatedPins });
      setSelectedPinNum(""); // Clear the selected pin to ensure UI updates
    }
    handleCloseModal();
  };

  const handleTabChange = (event, newValue) => {
    setSelectedTab(newValue);
  };

  return (
    <>
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

      <Dialog open={isModalOpen} onClose={handleCloseModal}>
        <DialogTitle>Add Module</DialogTitle>
        <DialogContent>
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
              onChange={(e) => setWebSocketURL(e.target.value)}
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
                  <MenuItem
                    key={pin}
                    value={pin}
                    onClick={() => {
                      if (config.pins && config.pins[pin]) {
                        // Removed unused handleOpenPinModal function
                      }
                    }}
                  >
                    GPIO{pin}
                  </MenuItem>
                ))}
              </Select>
            </FormControl>
          )}
        </DialogContent>
        <DialogActions>
          <IButton
            color="primary"
            Icon={AddTaskIcon}
            onClick={handleAdd}
            tooltip={"Add " + (selectedTab === 1 ? "WebSocket" : "Pin")}
            disabled={selectedTab === 1 ? !webSocketURL : selectedPinNum === ""}
          />
        </DialogActions>
      </Dialog>
    </>
  );
}
