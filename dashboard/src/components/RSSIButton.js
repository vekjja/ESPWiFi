import React, { useState, useEffect } from "react";
import { Fab, Tooltip } from "@mui/material";
import SignalCellularAltIcon from "@mui/icons-material/SignalCellularAlt";
import SignalCellularAlt1BarIcon from "@mui/icons-material/SignalCellularAlt1Bar";
import SignalCellularAlt2BarIcon from "@mui/icons-material/SignalCellularAlt2Bar";
import RSSISettingsModal from "./RSSISettingsModal";

export default function RSSIButton({
  config,
  deviceOnline,
  saveConfig,
  saveConfigToDevice,
  onRSSIDataChange,
  getRSSIColor,
  controlRssi = null,
  musicPlaybackState = { isPlaying: false, isPaused: false },
}) {
  const [modalOpen, setModalOpen] = useState(false);
  const [rssiValue, setRssiValue] = useState(null);

  // Consume latest RSSI from control channel (polling is handled in App.js)
  useEffect(() => {
    if (typeof controlRssi === "number") {
      setRssiValue(controlRssi);
      if (onRSSIDataChange) onRSSIDataChange(controlRssi, true);
    } else if (controlRssi === null) {
      // Clear RSSI value when device is offline
      if (!deviceOnline) {
        setRssiValue(null);
        if (onRSSIDataChange) onRSSIDataChange(null, false);
      }
    }
  }, [controlRssi, deviceOnline, onRSSIDataChange]);

  // Get the appropriate signal icon based on RSSI value
  const getRSSIIconComponent = (rssiValue) => {
    if (rssiValue === null || rssiValue === undefined) {
      return <SignalCellularAltIcon />;
    }
    if (rssiValue >= -60) return <SignalCellularAltIcon />;
    if (rssiValue >= -70) return <SignalCellularAlt2BarIcon />;
    if (rssiValue >= -80) return <SignalCellularAlt1BarIcon />;
    return <SignalCellularAltIcon />;
  };

  // Get RSSI status text
  const getRSSIStatusText = (rssiValue) => {
    if (musicPlaybackState.isPlaying)
      return "RSSI disabled during music playback";
    if (!deviceOnline) return "Control socket disconnected";
    if (rssiValue === null || rssiValue === undefined)
      return "Connected, waiting for data...";
    return `RSSI: ${rssiValue} dBm`;
  };

  const buttonProps = {
    size: "medium",
    color: "primary",
  };

  const handleClick = () => {
    if (deviceOnline && !musicPlaybackState.isPlaying) {
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  // Button is disabled if device offline OR music is playing
  const isDisabled = !deviceOnline || musicPlaybackState.isPlaying;

  const button = (
    <Fab
      {...buttonProps}
      onClick={isDisabled ? undefined : handleClick}
      disabled={isDisabled}
      sx={{
        color: isDisabled
          ? "text.disabled"
          : getRSSIColor
          ? getRSSIColor(rssiValue)
          : "primary.main",
        backgroundColor: isDisabled ? "action.disabled" : "action.hover",
        "&:hover": {
          backgroundColor: isDisabled ? "action.disabled" : "action.selected",
        },
      }}
    >
      {getRSSIIconComponent(rssiValue)}
    </Fab>
  );

  // Wrap disabled buttons in a span to fix MUI Tooltip warning
  if (isDisabled) {
    return (
      <Tooltip title={getRSSIStatusText(rssiValue)}>
        <span>{button}</span>
      </Tooltip>
    );
  }

  return (
    <>
      <Tooltip title={getRSSIStatusText(rssiValue)}>{button}</Tooltip>

      {modalOpen && (
        <RSSISettingsModal
          config={config}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          open={modalOpen}
          onClose={handleCloseModal}
          rssiValue={rssiValue}
        />
      )}
    </>
  );
}
