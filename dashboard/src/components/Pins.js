import React, { useState, useEffect } from "react";
import Switch from "@mui/material/Switch";
import FormControlLabel from "@mui/material/FormControlLabel";
import {
  Container,
  MenuItem,
  TextField,
  FormControl,
  InputLabel,
  Select,
  Slider,
  Box,
  Checkbox,
} from "@mui/material";
import SaveButton from "./SaveButton";
import DeleteButton from "./DeleteButton";
import Module from "./Module";
import SettingsModal from "./SettingsModal";

// Pin component inlined from Pin.js
function Pin({ config, pinNum, props, updatePins }) {
  const [isOn, setIsOn] = useState(props.state === "high");
  const [name, setName] = useState(props.name || "Pin");
  const [mode, setMode] = useState(props.mode || "out");
  const [hz] = useState(props.hz || 50);
  const [cycle] = useState(props.cycle || 20000);
  const [inverted, setInverted] = useState(props.inverted || false);
  const [anchorEl] = useState(null);
  const [openPinModal, setOpenPinModal] = useState(false);
  const [editedPinName, setEditedPinName] = useState(name);
  const [editedMode, setEditedMode] = useState(mode);
  const [editedHz, setEditedHz] = useState(hz);
  const [editedCycle, setEditedCycle] = useState(cycle);
  const [editedInverted, setEditedInverted] = useState(inverted);

  const dutyMin = props.dutyMin || 0;
  const dutyMax = props.dutyMax || 100;
  const [editedDutyMin, setEditedDutyMin] = useState(dutyMin);
  const [editedDutyMax, setEditedDutyMax] = useState(dutyMax);
  const [sliderMin, setSliderMin] = useState(props.dutyMin || dutyMin);
  const [sliderMax, setSliderMax] = useState(props.dutyMax || dutyMax);
  const [duty, setDuty] = useState(props.duty || 0);

  const updatePinState = (newState, deletePin) => {
    const pinState = {
      name: name,
      mode: mode,
      hz: hz,
      duty: duty,
      cycle: cycle,
      inverted: inverted,
      state: isOn ? "high" : "low",
      ...newState,
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
        num: parseInt(pinNum, 10),
      }),
    })
      .then((response) => {
        if (!response.ok) {
          throw new Error("Failed to update pin state");
        }
        return response.json();
      })
      .then((data) => {
        updatePins(pinState, deletePin);
      })
      .catch((error) => {
        console.error("Error updating pin state:", error);
      });
  };

  const handleChange = (event) => {
    const uiState = event.target.checked;
    // When inverted, UI ON means actual LOW, UI OFF means actual HIGH
    const actualState = inverted
      ? uiState
        ? "low"
        : "high"
      : uiState
      ? "high"
      : "low";
    // Update the internal state to match the actual pin state
    setIsOn(actualState === "high");
    updatePinState({ state: actualState });
  };

  const handleOpenPinModal = () => {
    setEditedPinName(name);
    setEditedMode(mode);
    setEditedHz(hz);
    setEditedCycle(cycle);
    setEditedInverted(inverted);
    setOpenPinModal(true);
  };

  const handleClosePinModal = () => {
    setOpenPinModal(false);
  };

  const handleSavePinSettings = () => {
    const newInverted = editedInverted;
    setName(editedPinName);
    setMode(editedMode);
    setInverted(newInverted);

    // Create a temporary pinState with the new inverted value for the API call
    const tempPinState = {
      name: editedPinName,
      mode: editedMode,
      hz: editedHz,
      duty: duty,
      cycle: editedCycle,
      inverted: newInverted,
      state: isOn ? "high" : "low",
    };

    fetch(`${config.apiURL}/gpio`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        ...tempPinState,
        num: parseInt(pinNum, 10),
      }),
    })
      .then((response) => {
        if (!response.ok) {
          throw new Error("Failed to update pin state");
        }
        return response.json();
      })
      .then((data) => {
        updatePins(tempPinState, false);
      })
      .catch((error) => {
        console.error("Error updating pin state:", error);
      });

    setSliderMin(editedDutyMin);
    setSliderMax(editedDutyMax);
    handleClosePinModal();
  };

  useEffect(() => {
    setEditedDutyMin(props.dutyMin || 0);
    setEditedDutyMax(props.dutyMax || 255);
  }, [props.dutyMin, props.dutyMax]);

  useEffect(() => {
    if (duty < editedDutyMin) setDuty(Number(editedDutyMin));
    if (duty > editedDutyMax) setDuty(Number(editedDutyMax));
  }, [editedDutyMin, editedDutyMax]);

  const effectiveIsOn = inverted ? !isOn : isOn;

  return (
    <Module
      title={name || pinNum}
      onSettings={handleOpenPinModal}
      settingsTooltip={"Pin Settings"}
      sx={{
        backgroundColor: anchorEl
          ? "primary.dark"
          : effectiveIsOn
          ? "secondary.light"
          : "secondary.dark",
        borderColor: effectiveIsOn ? "primary.main" : "secondary.main",
        maxWidth: "200px",
      }}
    >
      <SettingsModal
        open={openPinModal}
        onClose={handleClosePinModal}
        title="Pin Settings"
        actions={
          <>
            <DeleteButton
              onClick={() => updatePinState({}, "DELETE")}
              tooltip={"Delete Pin"}
            />
            <SaveButton
              onClick={handleSavePinSettings}
              tooltip={"Save Pin Settings"}
            />
          </>
        }
      >
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
        <FormControlLabel
          control={
            <Checkbox
              checked={editedInverted}
              onChange={(e) => setEditedInverted(e.target.checked)}
            />
          }
          label="Invert"
          sx={{ marginBottom: 2 }}
        />
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
      </SettingsModal>

      <FormControlLabel
        labelPlacement="top"
        control={
          <Switch
            checked={!!effectiveIsOn}
            onChange={handleChange}
            disabled={props.mode === "in"}
          />
        }
        value={effectiveIsOn}
      />

      {mode === "pwm" && (
        <Box sx={{ marginTop: "10px" }}>
          <Slider
            value={duty}
            onChange={(event, newValue) => setDuty(newValue)}
            onChangeCommitted={(event, newValue) => {
              updatePinState({ duty: newValue });
            }}
            aria-labelledby="duty-length-slider"
            min={Number(sliderMin)}
            max={Number(sliderMax)}
            valueLabelDisplay="auto"
          />
        </Box>
      )}
    </Module>
  );
}

// Pins container
export default function Pins({ config, saveConfig }) {
  const [pins, setPins] = useState({});

  useEffect(() => {
    if (config && config.pins) {
      setPins(config.pins);
    } else if (config && !config.pins) {
      setPins({});
    }
  }, [config]);

  const pinElements = Object.keys(pins).map((key) => {
    const pin = pins[key];
    return (
      <Pin
        key={key}
        config={config}
        pinNum={key}
        props={pin}
        updatePins={(pinState, deletePin) => {
          const updatedPins = { ...pins };
          if (deletePin) {
            delete updatedPins[key];
          } else {
            updatedPins[key] = pinState;
          }
          setPins(updatedPins);
          saveConfig({ ...config, pins: updatedPins });
        }}
      />
    );
  });

  if (!config || !config.pins) {
    return <div>Loading configuration...</div>;
  }

  return (
    <Container
      sx={{ display: "flex", flexWrap: "wrap", justifyContent: "center" }}
    >
      {pinElements}
    </Container>
  );
}
