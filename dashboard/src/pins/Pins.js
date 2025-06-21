import React, { useState, useEffect } from "react";
import Pin from "./Pin";
import {
  Container,
  Fab,
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  MenuItem,
  Select,
  FormControl,
  InputLabel,
} from "@mui/material";
import AddIcon from "@mui/icons-material/Add";

export default function Pins({ config, saveConfig }) {
  const [pins, setPins] = useState({}); // Initialize with an empty object
  const [isModalOpen, setIsModalOpen] = useState(false); // Modal visibility state
  const [selectedPinNum, setSelectedPinNum] = useState(""); // Selected pin number in modal
  const [selectedPinLabel, setSelectedPinLabel] = useState(""); // Selected pin label in modal

  // Define available ESP32-C3 Super Mini pins
  const Pins = [
    { label: "GPIO0", value: 0 },
    { label: "GPIO1", value: 1 },
    { label: "GPIO2", value: 2 },
    { label: "GPIO3", value: 3 },
    { label: "GPIO4", value: 4 },
    { label: "GPIO5", value: 5 },
    { label: "GPIO6", value: 6 },
    { label: "GPIO7", value: 7 },
    { label: "GPIO8", value: 8 },
    { label: "GPIO9", value: 9 },
    { label: "GPIO10", value: 10 },
    { label: "GPIO11", value: 11 },
    { label: "GPIO12", value: 12 },
    { label: "GPIO13", value: 13 },
    { label: "GPIO14", value: 14 },
    { label: "GPIO15", value: 15 },
    { label: "GPIO16", value: 16 },
    { label: "GPIO17", value: 17 },
    { label: "GPIO18", value: 18 },
    { label: "GPIO19", value: 19 },
    { label: "GPIO20", value: 20 },
    { label: "GPIO21", value: 21 },
  ];

  // Filter available pins
  const pinOptions = Pins.filter(
    (pin) => !Object.keys(pins).includes(pin.value.toString())
  );

  useEffect(() => {
    if (config && config.pins) {
      setPins(config.pins);
    }
  }, [config]);

  useEffect(() => {
    if (
      pinOptions.length > 0 &&
      !pinOptions.some((pin) => pin.value === selectedPinNum)
    ) {
      setSelectedPinNum(pinOptions[0].value); // Default to the first available pin
    }
  }, [pinOptions, selectedPinNum]);

  const updatePins = (pinState, deletePin, pinKey) => {
    const key = pinKey !== undefined ? String(pinKey) : String(selectedPinNum);
    if (deletePin) {
      const updatedPins = { ...pins };
      delete updatedPins[key];
      setPins(updatedPins);
      saveConfig({ ...config, pins: updatedPins });
      return;
    }
    const updatedPins = { ...pins };
    updatedPins[key] = pinState;
    setPins(updatedPins);
    saveConfig({ ...config, pins: updatedPins });
  };

  const handleAddBtnClick = () => {
    setSelectedPinNum(""); // Reset the selected pin number
    setIsModalOpen(true); // Open the modal when the Fab button is pressed
  };

  const handleCloseModal = () => {
    setIsModalOpen(false); // Close the modal
  };

  const handleAddPin = () => {
    const newPinState = {
      state: "low",
      name: selectedPinLabel,
      mode: "out", // Default mode
      duty: 1860,
      cycle: 20000,
      hz: 50,
    };
    updatePins(newPinState, false, selectedPinNum);
    handleCloseModal(); // Close the modal after submission
  };

  const handlePinNumberChange = (event) => {
    const selectedValue = event.target.value;
    const selectedLabel =
      Pins.find((pin) => pin.value === selectedValue)?.label || "";
    setSelectedPinNum(selectedValue); // Update selected pin number
    setSelectedPinLabel(selectedLabel); // Update selected pin label
  };

  // Map over the keys of the pins object
  const pinElements = Object.keys(pins).map((key) => {
    const pin = pins[key];
    return (
      <Pin
        key={key}
        config={config}
        pinNum={key}
        props={pin}
        updatePins={(pinState, deletePin) =>
          updatePins(pinState, deletePin, key)
        }
      />
    );
  });

  return (
    <Container
      sx={{ display: "flex", flexWrap: "wrap", justifyContent: "center" }}
    >
      <Fab
        size="small"
        color="primary"
        aria-label="add"
        onClick={handleAddBtnClick}
        sx={{ position: "fixed", top: "20px", right: "20px" }}
      >
        <AddIcon />
      </Fab>

      {pinElements}

      <Dialog open={isModalOpen} onClose={handleCloseModal}>
        <DialogTitle>Add Pin</DialogTitle>
        <DialogContent>
          <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
            <InputLabel id="pin-select-label">Pin</InputLabel>
            <Select
              labelId="pin-select-label"
              value={
                pinOptions.some((pin) => pin.value === selectedPinNum)
                  ? selectedPinNum
                  : ""
              }
              label="Pin Number"
              onChange={handlePinNumberChange}
            >
              {pinOptions.map((pin) => (
                <MenuItem key={pin.value} value={pin.value}>
                  {pin.label}
                </MenuItem>
              ))}
            </Select>
          </FormControl>
        </DialogContent>
        <DialogActions>
          <Button onClick={handleCloseModal} color="error">
            Cancel
          </Button>
          <Button
            onClick={handleAddPin}
            color="primary"
            disabled={!selectedPinNum}
          >
            Add Pin
          </Button>
        </DialogActions>
      </Dialog>
    </Container>
  );
}
