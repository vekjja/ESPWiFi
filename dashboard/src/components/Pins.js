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
function Pin({ pinNum, props, updatePins, config }) {
  const [isOn, setIsOn] = useState(props.state === "high");
  const [name, setName] = useState(props.name || "Pin");
  const [mode, setMode] = useState(props.mode || "out");
  const [hz] = useState(props.hz || 50);
  const [cycle] = useState(props.cycle || 20000);
  const [inverted, setInverted] = useState(props.inverted || false);
  const [remoteURL, setRemoteURL] = useState(props.remoteURL || "");
  const [anchorEl] = useState(null);
  const [openPinModal, setOpenPinModal] = useState(false);
  const [editedPinName, setEditedPinName] = useState(name);
  const [editedMode, setEditedMode] = useState(mode);
  const [editedHz, setEditedHz] = useState(hz);
  const [editedCycle, setEditedCycle] = useState(cycle);
  const [editedInverted, setEditedInverted] = useState(inverted);
  const [editedRemoteURL, setEditedRemoteURL] = useState(remoteURL);

  const dutyMin = props.dutyMin || 0;
  const dutyMax = props.dutyMax || 100;
  const [editedDutyMin, setEditedDutyMin] = useState(dutyMin);
  const [editedDutyMax, setEditedDutyMax] = useState(dutyMax);
  const [sliderMin, setSliderMin] = useState(props.dutyMin || dutyMin);
  const [sliderMax, setSliderMax] = useState(props.dutyMax || dutyMax);
  const [duty, setDuty] = useState(props.duty || 0);

  const updatePinState = (newState, deletePin) => {
    // Use the new state if provided, otherwise use current state
    const newPinState = {
      name: name,
      mode: mode,
      hz: hz,
      cycle: cycle,
      inverted: inverted,
      remoteURL: remoteURL,
      state: newState.state || (isOn ? "high" : "low"),
      ...newState,
    };

    // Only include duty for PWM mode pins
    if (mode === "pwm") {
      newPinState.duty = duty;
    }

    // For immediate pin state changes (like toggling), send to ESP32
    // For configuration changes (like settings), only update local config
    if (config && (newState.state || newState.duty)) {
      // Determine the target URL - use remoteURL if specified, otherwise use apiURL or current hostname
      let targetURL;
      if (remoteURL && remoteURL.trim()) {
        // Ensure the remote URL has the correct protocol and path
        let cleanURL = remoteURL.trim();
        if (
          !cleanURL.startsWith("http://") &&
          !cleanURL.startsWith("https://")
        ) {
          cleanURL = "http://" + cleanURL;
        }
        if (!cleanURL.endsWith("/gpio")) {
          cleanURL = cleanURL.replace(/\/$/, "") + "/gpio";
        }
        targetURL = cleanURL;
      } else {
        // Use apiURL if available, otherwise use current hostname
        const baseURL = config.apiURL || window.location.origin;
        targetURL = `${baseURL}/gpio`;
      }

      // Prepare request body - only include duty for PWM mode
      const requestBody = {
        ...newPinState,
        mode: deletePin ? "in" : newPinState.mode,
        state: deletePin ? "low" : newPinState.state,
        num: parseInt(pinNum, 10),
        delete: deletePin === "DELETE" || deletePin === true,
      };

      // Only include duty in request for PWM mode
      if (mode === "pwm" && !deletePin) {
        requestBody.duty = duty;
      }

      fetch(targetURL, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify(requestBody),
      })
        .then((response) => {
          if (!response.ok) {
            throw new Error("Failed to update pin state");
          }
          return response.json();
        })
        .then((data) => {
          // Only update local state after successful API call
          updatePins(newPinState, deletePin);
        })
        .catch((error) => {
          console.error("Error updating pin state:", error);
          // Revert local state change if API call failed
          if (newState.state) {
            setIsOn(!isOn);
          }
        });
    } else {
      // Update local state only for configuration changes
      updatePins(newPinState, deletePin);
    }
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

    // Send the update request with the new state
    updatePinState({ state: actualState });
  };

  const handleOpenPinModal = () => {
    setEditedPinName(name);
    setEditedMode(mode);
    setEditedHz(hz);
    setEditedCycle(cycle);
    setEditedInverted(inverted);
    setEditedRemoteURL(remoteURL);
    setOpenPinModal(true);
  };

  const handleClosePinModal = () => {
    setOpenPinModal(false);
  };

  const handleSavePinSettings = () => {
    const newInverted = editedInverted;
    const newRemoteURL = editedRemoteURL;
    setName(editedPinName);
    setMode(editedMode);
    setInverted(newInverted);
    setRemoteURL(newRemoteURL);

    // Create a temporary pinState with the new values
    const tempPinState = {
      name: editedPinName,
      mode: editedMode,
      hz: editedHz,
      cycle: editedCycle,
      inverted: newInverted,
      remoteURL: newRemoteURL,
      state: isOn ? "high" : "low",
    };

    // Only include duty for PWM mode pins
    if (editedMode === "pwm") {
      tempPinState.duty = duty;
    }

    // Update local state only - no remote API calls for settings
    updatePins(tempPinState, false);

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

  // Create title with remote indicator
  const moduleTitle =
    remoteURL && remoteURL.trim() ? `${name || pinNum}` : name || pinNum;

  return (
    <Module
      title={moduleTitle}
      onSettings={handleOpenPinModal}
      settingsTooltip={
        remoteURL && remoteURL.trim()
          ? `Pin Settings (Remote: ${remoteURL})`
          : "Pin Settings"
      }
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
              tooltip={"Apply Pin Settings"}
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
        <TextField
          label="Remote GPIO URL (optional)"
          value={editedRemoteURL}
          onChange={(e) => setEditedRemoteURL(e.target.value)}
          variant="outlined"
          fullWidth
          placeholder="http://192.168.1.100 or esp32.local"
          helperText="Leave empty to use local ESP32. Include protocol (http://) for remote devices."
          sx={{ marginBottom: 2 }}
        />
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

          // Update the global config with the new pins state
          const updatedConfig = { ...config, pins: updatedPins };
          saveConfig(updatedConfig);
        }}
        config={config}
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
