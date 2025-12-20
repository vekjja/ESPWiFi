import React, { useState, useEffect } from "react";
import Switch from "@mui/material/Switch";
import FormControlLabel from "@mui/material/FormControlLabel";
import { Slider, Box } from "@mui/material";
import Module from "./Module";
import PinSettingsModal from "./PinSettingsModal";
import { getFetchOptions } from "../utils/apiUtils";

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
  const [inverted, setInverted] = useState(initialProps.inverted || false);
  const [remoteURL, setRemoteURL] = useState(initialProps.remoteURL || "");
  const [currentPinNum, setCurrentPinNum] = useState(pinNum);
  const [openPinModal, setOpenPinModal] = useState(false);

  // Pin settings data for the modal
  const [pinSettingsData, setPinSettingsData] = useState({
    name: initialProps.name || "Pin",
    pinNumber: pinNum,
    mode: initialProps.mode || "out",
    inverted: initialProps.inverted || false,
    remoteURL: initialProps.remoteURL || "",
    dutyMin: initialProps.dutyMin || 0,
    dutyMax: initialProps.dutyMax || 255,
    size: initialProps.size || "medium",
    width: initialProps.width || 240,
    height: initialProps.height || 240,
  });

  const dutyMin = initialProps.dutyMin || 0;
  const dutyMax = initialProps.dutyMax || 100;
  const [sliderMin, setSliderMin] = useState(initialProps.dutyMin || dutyMin);
  const [sliderMax, setSliderMax] = useState(initialProps.dutyMax || dutyMax);
  const [duty, setDuty] = useState(initialProps.duty || 0);

  // Use module key for updates
  const moduleKey = initialProps.key;

  const updatePinState = (newState, deletePin, saveConfig = true) => {
    // Use the new state if provided, otherwise use current state
    const newPinState = {
      name: name,
      mode: mode,
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
        num: parseInt(currentPinNum, 10),
        delete: deletePin === "DELETE" || deletePin === true,
      };

      // Only include duty in request for PWM mode
      if (mode === "pwm" && !deletePin) {
        requestBody.duty = duty;
      }

      fetch(
        targetURL,
        getFetchOptions({
          method: "POST",
          body: JSON.stringify(requestBody),
        })
      )
        .then((response) => {
          if (!response.ok) {
            throw new Error("Failed to update pin state");
          }
          return response.json();
        })
        .then((data) => {
          // Only update local state after successful API call
          if (deletePin) {
            onDelete(moduleKey);
          } else if (saveConfig) {
            onUpdate(moduleKey, newPinState);
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
        onDelete(moduleKey);
      } else if (saveConfig) {
        onUpdate(moduleKey, newPinState);
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

    // Send the update request with the new state (don't save config)
    updatePinState({ state: actualState }, false, false);
  };

  const handleOpenPinModal = () => {
    setPinSettingsData({
      name: name,
      pinNumber: currentPinNum,
      mode: mode,
      inverted: inverted,
      remoteURL: remoteURL,
      dutyMin: dutyMin,
      dutyMax: dutyMax,
    });
    setOpenPinModal(true);
  };

  const handleClosePinModal = () => {
    setOpenPinModal(false);
  };

  const handlePinDataChange = (changes) => {
    setPinSettingsData((prev) => ({ ...prev, ...changes }));
  };

  const handleSavePinSettings = () => {
    const newInverted = pinSettingsData.inverted;
    const newRemoteURL = pinSettingsData.remoteURL;
    const newPinNum = pinSettingsData.pinNumber;
    setName(pinSettingsData.name);
    setMode(pinSettingsData.mode);
    setInverted(newInverted);
    setRemoteURL(newRemoteURL);
    setCurrentPinNum(newPinNum);

    // Create a temporary pinState with the new values
    const tempPinState = {
      name: pinSettingsData.name,
      mode: pinSettingsData.mode,
      inverted: newInverted,
      remoteURL: newRemoteURL,
      state: isOn ? "high" : "low",
      number: newPinNum,
      size: pinSettingsData.size,
      width: pinSettingsData.width,
      height: pinSettingsData.height,
    };

    // Only include duty for PWM mode pins
    if (pinSettingsData.mode === "pwm") {
      tempPinState.duty = duty;
    }

    // Update local state only - no remote API calls for settings
    onUpdate(moduleKey, tempPinState);

    setSliderMin(pinSettingsData.dutyMin);
    setSliderMax(pinSettingsData.dutyMax);
    handleClosePinModal();
  };

  const handleDeletePin = () => {
    updatePinState({}, "DELETE");
    handleClosePinModal();
  };

  useEffect(() => {
    if (duty < pinSettingsData.dutyMin)
      setDuty(Number(pinSettingsData.dutyMin));
    if (duty > pinSettingsData.dutyMax)
      setDuty(Number(pinSettingsData.dutyMax));
  }, [pinSettingsData.dutyMin, pinSettingsData.dutyMax]);

  // Update currentPinNum when initialProps.number changes
  useEffect(() => {
    if (initialProps.number !== undefined) {
      setCurrentPinNum(initialProps.number);
    }
  }, [initialProps.number]);

  const effectiveIsOn = inverted ? !isOn : isOn;

  // Create title with remote indicator
  const moduleTitle =
    remoteURL && remoteURL.trim()
      ? `${name || currentPinNum}`
      : name || currentPinNum;

  // Size mapping
  const sizeMap = {
    small: { width: 180, height: 180 },
    medium: { width: 240, height: 240 },
    large: { width: 320, height: 320 },
  };
  const effectiveSize =
    pinSettingsData.size === "custom"
      ? { width: pinSettingsData.width, height: pinSettingsData.height }
      : sizeMap[pinSettingsData.size] || sizeMap.medium;

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
        display: "flex",
        flexDirection: "column",
        justifyContent: "center",
        alignItems: "center",
        minWidth: effectiveSize.width,
        maxWidth: effectiveSize.width,
        minHeight: effectiveSize.height,
        maxHeight: effectiveSize.height,
      }}
    >
      <Box
        sx={{
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          justifyContent: "center",
          flex: 1,
          width: "100%",
          padding: "10px",
        }}
      >
        <FormControlLabel
          labelPlacement="top"
          control={
            <Switch
              checked={!!effectiveIsOn}
              onChange={handleChange}
              disabled={mode === "in"}
              data-no-dnd="true"
            />
          }
          value={effectiveIsOn}
          sx={{
            margin: mode === "pwm" ? "0 0 10px 0" : "0",
          }}
        />

        {mode === "pwm" && (
          <Box
            sx={{
              width: "100%",
              padding: "0 5px",
              marginTop: "10px",
            }}
          >
            <Slider
              value={duty}
              onChange={(event, newValue) => setDuty(newValue)}
              onChangeCommitted={(event, newValue) => {
                updatePinState({ duty: newValue }, false, false);
              }}
              aria-labelledby="duty-length-slider"
              min={Number(sliderMin)}
              max={Number(sliderMax)}
              valueLabelDisplay="auto"
              data-no-dnd="true"
              sx={{
                width: "100%",
                maxWidth: "180px",
              }}
            />
          </Box>
        )}
      </Box>

      <PinSettingsModal
        open={openPinModal}
        onClose={handleClosePinModal}
        onSave={handleSavePinSettings}
        onDelete={handleDeletePin}
        pinData={pinSettingsData}
        onPinDataChange={handlePinDataChange}
      />
    </Module>
  );
}
