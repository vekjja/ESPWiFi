import React, { useState, useRef, useEffect } from "react";
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
  Modal,
  Box,
  Typography,
} from "@mui/material";
import SaveButton from "./SaveButton";
import DeleteButton from "./DeleteButton";
import SettingsButton from "./SettingsButton";

// Pin component inlined from Pin.js
function Pin({ config, pinNum, props, updatePins }) {
  const [isOn, setIsOn] = useState(props.state === "high");
  const [name, setName] = useState(props.name || "Pin");
  const [mode, setMode] = useState(props.mode || "out");
  const [hz] = useState(props.hz || 50);
  const [cycle] = useState(props.cycle || 20000);
  const [anchorEl] = useState(null);
  const [openPinModal, setOpenPinModal] = useState(false);
  const [editedPinName, setEditedPinName] = useState(name);
  const [editedMode, setEditedMode] = useState(mode);
  const [editedHz, setEditedHz] = useState(hz);
  const [editedCycle, setEditedCycle] = useState(cycle);
  const containerRef = useRef(null);

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
    const newIsOn = event.target.checked;
    setIsOn(newIsOn);
    updatePinState({ state: newIsOn ? "high" : "low" });
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
    setName(editedPinName);
    setMode(editedMode);
    updatePinState({
      name: editedPinName,
      mode: editedMode,
      hz: editedHz,
      cycle: editedCycle,
      dutyMin: editedDutyMin,
      dutyMax: editedDutyMax,
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
          <DeleteButton
            onClick={() => updatePinState({}, "DELETE")}
            tooltip={"Delete Pin"}
          />
          <SaveButton
            onClick={handleSavePinSettings}
            tooltip={"Save Pin Settings"}
          />
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
            min={Number(sliderMin)}
            max={Number(sliderMax)}
            valueLabelDisplay="auto"
          />
        </Container>
      )}

      <SettingsButton
        onClick={handleOpenPinModal}
        tooltip="Pin Settings"
        color="secondary"
        sx={{ position: "absolute", left: 0, bottom: 0, m: 1 }}
      />
    </Container>
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
