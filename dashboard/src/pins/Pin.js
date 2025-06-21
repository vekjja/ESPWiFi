import React, { useState, useRef } from "react";
import Switch from "@mui/material/Switch";
import FormControlLabel from "@mui/material/FormControlLabel";
import {
  Container,
  IconButton,
  Menu,
  MenuItem,
  TextField,
  FormControl,
  InputLabel,
  Select,
  Slider,
} from "@mui/material";
import DeleteIcon from "@mui/icons-material/Delete";
import EarbudsIcon from "@mui/icons-material/Earbuds";
import OutputIcon from "@mui/icons-material/Output";
import InputIcon from "@mui/icons-material/Input";
import BuildIcon from "@mui/icons-material/Build"; // Generic icon for modes without a specific icon

export default function Pin({ config, pinNum, props, updatePins }) {
  const [isOn, setIsOn] = useState(props.state === "high"); // Initialize with a boolean
  const [name, setName] = useState(props.name || "Pin"); // Default to pin
  const [mode, setMode] = useState(props.mode || "out"); // Default to "out"
  const [hz, setHz] = useState(props.hz || 50); // Default to 50
  const [duty, setDuty] = useState(props.duty || 0); // Default to 0
  const [cycle, setCycle] = useState(props.cycle || 20000); // Default to 20000
  const [anchorEl, setAnchorEl] = useState(null);
  const [dutyMax, setDutyMax] = useState(255);
  const [dutyMin, setDutyMin] = useState(0);
  const containerRef = useRef(null);

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
        updatePins(pinState, deletePin); // Call the parent's `handleUpdatePin`
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

  const handleNameChange = (event) => {
    const newName = event.target.value;
    setName(newName);
  };

  const handleNameSubmit = () => {
    updatePinState({ name });
    handleMenuClose();
  };

  const handleMenuOpen = () => {
    setAnchorEl(containerRef.current);
  };

  const handleMenuClose = () => {
    setAnchorEl(null);
  };

  const handleDelete = () => {
    updatePinState({}, "DELETE");
    handleMenuClose();
  };

  const handleModeChange = (event) => {
    const newMode = event.target.value;
    setMode(newMode);
    updatePinState({ mode: newMode });
  };

  const handleFrequencyChange = (event) => {
    const newFrequency = event.target.value;
    setHz(newFrequency);
    updatePinState({ hz: newFrequency });
  };

  const handleCycleChange = (event, newValue) => {
    setCycle(newValue);
    updatePinState({ cycle: newValue });
  };

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
        onClick={handleMenuOpen}
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
      <Menu
        id="pin-settings-menu"
        anchorEl={anchorEl}
        open={Boolean(anchorEl)}
        onClose={handleMenuClose}
        anchorOrigin={{
          vertical: "bottom",
          horizontal: "center",
        }}
        transformOrigin={{
          vertical: "top",
          horizontal: "center",
        }}
      >
        <MenuItem>
          <TextField
            id="standard-basic"
            label="Pin"
            variant="outlined"
            value={pinNum}
            disabled
          />
        </MenuItem>
        <MenuItem>
          <TextField
            id="standard-basic"
            label="Name"
            variant="outlined"
            value={name}
            onChange={handleNameChange}
            onBlur={handleNameSubmit}
          />
        </MenuItem>
        <MenuItem>
          <FormControl fullWidth variant="outlined">
            <InputLabel id="mode-select-label">Mode</InputLabel>
            <Select
              labelId="mode-select-label"
              value={mode}
              label="Mode"
              onChange={handleModeChange}
            >
              <MenuItem value="in">Input</MenuItem>
              <MenuItem value="out">Output</MenuItem>
              <MenuItem value="pwm">PWM</MenuItem>
              {/* <MenuItem value="alt0">Alt0</MenuItem>
                            <MenuItem value="alt1">Alt1</MenuItem>
                            <MenuItem value="alt2">Alt2</MenuItem>
                            <MenuItem value="alt3">Alt3</MenuItem>
                            <MenuItem value="alt4">Alt4</MenuItem>
                            <MenuItem value="alt5">Alt5</MenuItem> */}
            </Select>
          </FormControl>
        </MenuItem>

        {mode === "pwm" && (
          <Container>
            <MenuItem>
              <TextField
                id="standard-basic"
                label="Frequency (Hz)"
                variant="outlined"
                value={hz}
                onChange={handleFrequencyChange}
              />
            </MenuItem>
            <MenuItem>
              <TextField
                id="standard-basic"
                label="Min Duty (µs)"
                variant="outlined"
                value={dutyMin}
                onChange={(e) => setDutyMin(Number(e.target.value))}
              />
            </MenuItem>
            <MenuItem>
              <TextField
                id="standard-basic"
                label="Max Duty (µs)"
                variant="outlined"
                value={dutyMax}
                onChange={(e) => setDutyMax(Number(e.target.value))}
              />
            </MenuItem>
            <MenuItem>
              <TextField
                id="standard-basic"
                label="Cycle (µs)"
                variant="outlined"
                value={cycle}
                onChange={handleCycleChange}
              />
            </MenuItem>
          </Container>
        )}

        <MenuItem onClick={handleDelete}>
          <DeleteIcon color="error" />
        </MenuItem>
      </Menu>
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
            onChange={(event, newValue) => setDuty(newValue)} // Update local state while dragging
            onChangeCommitted={(event, newValue) => {
              updatePinState({ duty: newValue }); // Update only when slider is released
            }}
            aria-labelledby="duty-length-slider"
            min={Number(dutyMin)}
            max={Number(dutyMax)}
            valueLabelDisplay="auto"
          />
        </Container>
      )}
    </Container>
  );
}
