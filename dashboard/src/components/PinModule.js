import React, { useState, useEffect } from "react";
import Switch from "@mui/material/Switch";
import FormControlLabel from "@mui/material/FormControlLabel";
import {
  MenuItem,
  TextField,
  FormControl,
  InputLabel,
  Select,
  Slider,
  Box,
  Checkbox,
} from "@mui/material";
import OkayButton from "./OkayButton";
import DeleteButton from "./DeleteButton";
import Module from "./Module";
import SettingsModal from "./SettingsModal";

export default function PinModule({
  pinNum,
  initialProps,
  config,
  onUpdate,
  onDelete,
}) {
  const [isOn, setIsOn] = useState(initialProps.state === "high");
  const [name, setName] = useState(initialProps.name || "Pin");
  const [mode, setMode] = useState(initialProps.mode || "out");
  const [hz] = useState(initialProps.hz || 50);
  const [cycle] = useState(initialProps.cycle || 20000);
  const [inverted, setInverted] = useState(initialProps.inverted || false);
  const [remoteURL, setRemoteURL] = useState(initialProps.remoteURL || "");
  const [openPinModal, setOpenPinModal] = useState(false);
  const [editedPinName, setEditedPinName] = useState(name);
  const [editedMode, setEditedMode] = useState(mode);
  const [editedHz, setEditedHz] = useState(hz);
  const [editedCycle, setEditedCycle] = useState(cycle);
  const [editedInverted, setEditedInverted] = useState(inverted);
  const [editedRemoteURL, setEditedRemoteURL] = useState(remoteURL);

  const dutyMin = initialProps.dutyMin || 0;
  const dutyMax = initialProps.dutyMax || 100;
  const [editedDutyMin, setEditedDutyMin] = useState(dutyMin);
  const [editedDutyMax, setEditedDutyMax] = useState(dutyMax);
  const [sliderMin, setSliderMin] = useState(initialProps.dutyMin || dutyMin);
  const [sliderMax, setSliderMax] = useState(initialProps.dutyMax || dutyMax);
  const [duty, setDuty] = useState(initialProps.duty || 0);

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
          if (deletePin) {
            onDelete(pinNum);
          } else {
            onUpdate(pinNum, newPinState);
          }
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
      if (deletePin) {
        onDelete(pinNum);
      } else {
        onUpdate(pinNum, newPinState);
      }
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
    onUpdate(pinNum, tempPinState);

    setSliderMin(editedDutyMin);
    setSliderMax(editedDutyMax);
    handleClosePinModal();
  };

  useEffect(() => {
    setEditedDutyMin(initialProps.dutyMin || 0);
    setEditedDutyMax(initialProps.dutyMax || 255);
  }, [initialProps.dutyMin, initialProps.dutyMax]);

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
        backgroundColor: effectiveIsOn ? "secondary.light" : "secondary.dark",
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
            <OkayButton
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
            disabled={mode === "in"}
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
