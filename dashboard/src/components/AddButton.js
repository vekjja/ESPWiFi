import React, { useState } from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  FormControlLabel,
  Switch,
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  Fab,
  Button,
} from "@mui/material";
import AddIcon from "@mui/icons-material/Add";

export default function AddButton({ config, saveConfig }) {
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [isAddingWebSocket, setIsAddingWebSocket] = useState(false);
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
    setIsAddingWebSocket(false);
    setIsModalOpen(true);
  };

  const handleCloseModal = () => {
    setIsModalOpen(false);
  };

  const handleAdd = () => {
    if (isAddingWebSocket) {
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

  return (
    <>
      <Fab
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
          <FormControlLabel
            control={
              <Switch
                checked={isAddingWebSocket}
                onChange={(e) => setIsAddingWebSocket(e.target.checked)}
              />
            }
            label={isAddingWebSocket ? "Add WebSocket" : "Add Pin"}
          />

          {isAddingWebSocket ? (
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
                  <MenuItem key={pin} value={pin}>
                    GPIO{pin}
                  </MenuItem>
                ))}
              </Select>
            </FormControl>
          )}
        </DialogContent>
        <DialogActions>
          <Button onClick={handleCloseModal} color="error">
            Cancel
          </Button>
          <Button
            onClick={handleAdd}
            color="primary"
            disabled={isAddingWebSocket ? !webSocketURL : selectedPinNum === ""}
          >
            Add
          </Button>
        </DialogActions>
      </Dialog>
    </>
  );
}
