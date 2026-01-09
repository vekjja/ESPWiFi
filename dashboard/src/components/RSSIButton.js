import React, { useState, useEffect, useRef } from "react";
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
  onRequestRssi = null,
}) {
  const [modalOpen, setModalOpen] = useState(false);
  const [rssiValue, setRssiValue] = useState(null);
  const pollRef = useRef(null);

  // Cleanup polling on unmount
  useEffect(() => {
    return () => {
      if (pollRef.current) {
        clearInterval(pollRef.current);
        pollRef.current = null;
      }
    };
  }, []);

  // RSSI comes from /ws/control via {cmd:"get_rssi"}.
  // Poll while device is online (lightweight; no extra websocket).
  useEffect(() => {
    if (!deviceOnline) {
      if (pollRef.current) {
        clearInterval(pollRef.current);
        pollRef.current = null;
      }
      setRssiValue(null);
      if (onRSSIDataChange) onRSSIDataChange(null, false);
      return;
    }

    // Kick once immediately, then poll.
    if (typeof onRequestRssi === "function") {
      onRequestRssi();
    }
    if (pollRef.current) clearInterval(pollRef.current);
    pollRef.current = setInterval(() => {
      if (typeof onRequestRssi === "function") onRequestRssi();
    }, 1000);

    return () => {
      if (pollRef.current) {
        clearInterval(pollRef.current);
        pollRef.current = null;
      }
    };
  }, [deviceOnline, onRequestRssi, onRSSIDataChange]);

  // Consume latest RSSI from control channel
  useEffect(() => {
    if (typeof controlRssi === "number") {
      setRssiValue(controlRssi);
      if (onRSSIDataChange) onRSSIDataChange(controlRssi, true);
    }
  }, [controlRssi, onRSSIDataChange]);

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
    if (rssiValue === null || rssiValue === undefined)
      return "Connected, waiting for data...";
    return `RSSI: ${rssiValue} dBm`;
  };

  const buttonProps = {
    size: "medium",
    color: "primary",
  };

  const handleClick = () => {
    if (deviceOnline) {
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  const button = (
    <Fab
      {...buttonProps}
      onClick={deviceOnline ? handleClick : undefined}
      disabled={!deviceOnline}
      sx={{
        color: !deviceOnline
          ? "text.disabled"
          : getRSSIColor
          ? getRSSIColor(rssiValue)
          : "primary.main",
        backgroundColor: !deviceOnline ? "action.disabled" : "action.hover",
        "&:hover": {
          backgroundColor: !deviceOnline
            ? "action.disabled"
            : "action.selected",
        },
      }}
    >
      {getRSSIIconComponent(rssiValue)}
    </Fab>
  );

  // Wrap disabled buttons in a span to fix MUI Tooltip warning
  if (!deviceOnline) {
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
          deviceOnline={deviceOnline}
          open={modalOpen}
          onClose={handleCloseModal}
          onRSSIDataChange={(value, connected) => {
            setRssiValue(value);
            if (onRSSIDataChange) {
              onRSSIDataChange(value, connected);
            }
          }}
        />
      )}
    </>
  );
}
