import React, { useState, useRef, useEffect } from "react";
import Switch from "@mui/material/Switch";
import FormControlLabel from "@mui/material/FormControlLabel";
import {
  Container,
  IconButton,
  MenuItem,
  TextField,
  FormControl,
  InputLabel,
  Select,
  Slider,
  Modal,
  Box,
  Typography,
} from "@mui/material";
import DeleteIcon from "@mui/icons-material/Delete";
import SaveIcon from "@mui/icons-material/Save";
import EarbudsIcon from "@mui/icons-material/Earbuds";
import OutputIcon from "@mui/icons-material/Output";
import InputIcon from "@mui/icons-material/Input";
import BuildIcon from "@mui/icons-material/Build"; // Generic icon for modes without a specific icon

export default function Pin({ config, pinNum, props, updatePins }) {
  const [isOn, setIsOn] = useState(props.state === "high"); // Initialize with a boolean
  const [name] = useState(props.name || "Pin"); // Default to pin
  const [mode, setMode] = useState(props.mode || "out"); // Default to "out"
  const [hz] = useState(props.hz || 50); // Default to 50
  const [cycle] = useState(props.cycle || 20000); // Default to 20000
  const [anchorEl] = useState(null);
  const [openPinModal, setOpenPinModal] = useState(false);
  const [editedPinName, setEditedPinName] = useState(name);
  const [editedMode, setEditedMode] = useState(mode);
  const [editedHz, setEditedHz] = useState(hz);
  const [editedCycle, setEditedCycle] = useState(cycle);
  const containerRef = useRef(null);

  // Define dutyMin and dutyMax before use
  const dutyMin = props.dutyMin || 0;
  const dutyMax = props.dutyMax || 100;

  const [editedDutyMin, setEditedDutyMin] = useState(dutyMin); // Initialize with default
  const [editedDutyMax, setEditedDutyMax] = useState(dutyMax); // Initialize with default

  // Update the slider's min and max dynamically
  const [sliderMin, setSliderMin] = useState(props.dutyMin || dutyMin);
  const [sliderMax, setSliderMax] = useState(props.dutyMax || dutyMax);

  // Reintroduce duty state for slider functionality
  const [duty, setDuty] = useState(props.duty || 0); // Default to 0

  const updatePinState = (newState, deletePin) => {
    const pinState = {
      name: name,
      mode: mode,
      hz: hz,
      duty: duty,
      cycle: cycle,
      state: isOn ? "high" : "low", // Map the current "on" state to "state"
      ...newState, // Merge any additional state values
    };
    fetch(`${config.apiURL}/gpio`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        ...pinState,
        mode: deletePin ? "in" : pinState.mode,
        state: deletePin ? "low" : pinState.state,
        num: parseInt(pinNum, 10), // Only for backend, not for config
      }),
    })
      .then((response) => {
        if (!response.ok) {
          throw new Error("Failed to update pin state");
        }
        return response.json();
      })
      .then((data) => {
        updatePins(pinState, deletePin); // Revert to original updatePins call
      })
      .catch((error) => {
        console.error("Error updating pin state:", error);
      });
  };

  const handleChange = (event) => {
    const newIsOn = event.target.checked;
    setIsOn(newIsOn);
    updatePinState({ state: newIsOn ? "high" : "low" }); // Pass updated `state`
  };

  const handleOpenPinModal = () => {
    setEditedPinName(name);
    setEditedMode(mode);
    setEditedHz(hz);
    setEditedCycle(cycle);
    setOpenPinModal(true);
  };

  const handleClosePinModal = () => {
    setOpenPinModal(false);
  };

  const handleSavePinSettings = () => {
    setMode(editedMode); // Update mode state immediately after saving
    updatePinState({
      name: editedPinName,
      mode: editedMode,
      hz: editedHz,
      cycle: editedCycle,
      dutyMin: editedDutyMin, // Save min duty
      dutyMax: editedDutyMax, // Save max duty
    });
    setSliderMin(editedDutyMin); // Update slider min
    setSliderMax(editedDutyMax); // Update slider max
    handleClosePinModal();
  };

  useEffect(() => {
    setEditedDutyMin(props.dutyMin || 0); // Revert to initializing min duty from props
    setEditedDutyMax(props.dutyMax || 255); // Revert to initializing max duty from props
  }, [props.dutyMin, props.dutyMax]);

  useEffect(() => {
    if (duty < editedDutyMin) setDuty(Number(editedDutyMin)); // Ensure duty is within range
    if (duty > editedDutyMax) setDuty(Number(editedDutyMax));
  }, [editedDutyMin, editedDutyMax]);

  // Determine the icon based on the mode
  const getIconForMode = (mode) => {
    switch (mode) {
      case "in":
        return (
          <InputIcon
            sx={{ color: isOn ? "primary.dark" : "secondary.light" }}
          />
        );
      case "out":
        return (
          <OutputIcon
            sx={{ color: isOn ? "primary.dark" : "secondary.light" }}
          />
        );
      case "pwm":
        return (
          <EarbudsIcon
            sx={{ color: isOn ? "primary.dark" : "secondary.light" }}
          />
        );
      default:
        return (
          <BuildIcon
            sx={{ color: isOn ? "primary.dark" : "secondary.light" }}
          />
        ); // Fallback icon
    }
  };

  return (
    <Container
      ref={containerRef}
      sx={{
        padding: "10px",
        margin: "10px",
        border: "1px solid",
        backgroundColor: anchorEl
          ? "primary.dark"
          : isOn
          ? "secondary.light"
          : "secondary.dark",
        borderColor: isOn ? "primary.main" : "secondary.main",
        borderRadius: "5px",
        position: "relative",
        maxWidth: "200px",
      }}
    >
      <IconButton
        aria-controls="pin-settings-menu"
        aria-haspopup="true"
        onClick={handleOpenPinModal}
        sx={{
          position: "absolute",
          top: 0,
          left: 0,
          margin: "5px",
          maxHeight: "9px",
        }}
      >
        {getIconForMode(mode)}
      </IconButton>

      <Modal open={openPinModal} onClose={handleClosePinModal}>
        <Box
          sx={{
            position: "absolute",
            top: "50%",
            left: "50%",
            transform: "translate(-50%, -50%)",
            width: 300,
            bgcolor: "background.paper",
            boxShadow: 24,
            p: 4,
            borderRadius: 2,
          }}
        >
          <Typography variant="h6" gutterBottom>
            Pin Settings
          </Typography>
          <TextField
            label="Name"
            value={editedPinName}
            onChange={(e) => setEditedPinName(e.target.value)}
            variant="outlined"
            fullWidth
            sx={{ marginBottom: 2 }}
          />
          <TextField
            label="Pin Number"
            value={pinNum}
            variant="outlined"
            fullWidth
            disabled
            sx={{ marginBottom: 2 }}
          />
          <FormControl fullWidth variant="outlined" sx={{ marginBottom: 2 }}>
            <InputLabel id="mode-select-label">Mode</InputLabel>
            <Select
              labelId="mode-select-label"
              value={editedMode}
              onChange={(e) => setEditedMode(e.target.value)}
            >
              <MenuItem value="in">Input</MenuItem>
              <MenuItem value="out">Output</MenuItem>
              <MenuItem value="pwm">PWM</MenuItem>
            </Select>
          </FormControl>
          {editedMode === "pwm" && (
            <>
              <TextField
                label="Frequency (Hz)"
                value={editedHz}
                onChange={(e) => setEditedHz(e.target.value)}
                variant="outlined"
                fullWidth
                sx={{ marginBottom: 2 }}
              />
              <TextField
                label="Min Duty (µs)"
                value={editedDutyMin}
                onChange={(e) => setEditedDutyMin(e.target.value)}
                variant="outlined"
                fullWidth
                sx={{ marginBottom: 2 }}
              />
              <TextField
                label="Max Duty (µs)"
                value={editedDutyMax}
                onChange={(e) => setEditedDutyMax(e.target.value)}
                variant="outlined"
                fullWidth
                sx={{ marginBottom: 2 }}
              />
              <TextField
                label="Cycle (µs)"
                value={editedCycle}
                onChange={(e) => setEditedCycle(e.target.value)}
                variant="outlined"
                fullWidth
                sx={{ marginBottom: 2 }}
              />
            </>
          )}
          <IconButton
            color="error"
            onClick={() => updatePinState({}, "DELETE")}
            sx={{ mt: 2, ml: 2 }}
          >
            <DeleteIcon />
          </IconButton>
          <IconButton
            color="primary"
            onClick={handleSavePinSettings}
            sx={{ mt: 2, ml: 2 }}
          >
            <SaveIcon />
          </IconButton>
        </Box>
      </Modal>

      <FormControlLabel
        labelPlacement="top"
        label={name || pinNum}
        control={
          <Switch
            checked={!!isOn}
            onChange={handleChange}
            disabled={props.mode === "in"}
          />
        }
        value={isOn}
      />

      {mode === "pwm" && (
        <Container sx={{ marginTop: "10px" }}>
          <Slider
            value={duty}
            onChange={(event, newValue) => setDuty(newValue)}
            onChangeCommitted={(event, newValue) => {
              updatePinState({ duty: newValue });
            }}
            aria-labelledby="duty-length-slider"
            min={Number(sliderMin)} // Use dynamic min
            max={Number(sliderMax)} // Use dynamic max
            valueLabelDisplay="auto"
          />
        </Container>
      )}
    </Container>
  );
}
