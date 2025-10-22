import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import {
  SignalCellularAlt as SignalCellularAltIcon,
  SignalCellularAlt1Bar as SignalCellularAlt1BarIcon,
  SignalCellularAlt2Bar as SignalCellularAlt2BarIcon,
} from "@mui/icons-material";
import RSSISettingsModal from "./RSSISettingsModal";

export default function RSSIButton({
  config,
  deviceOnline,
  onRSSISettings,
  rssiValue,
  rssiEnabled,
  rssiDisplayMode,
  getRSSIColor,
  getRSSIIcon,
}) {
  const [modalOpen, setModalOpen] = useState(false);

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
  const getRSSIStatusText = (rssiValue, rssiEnabled) => {
    if (!rssiEnabled) return "RSSI - Disabled";
    if (rssiValue === null || rssiValue === undefined)
      return "Connected, waiting for data...";
    return `RSSI: ${rssiValue} dBm`;
  };

  const buttonProps = {
    size: "medium",
    color: "primary",
  };

  const handleClick = () => {
    if (onRSSISettings) {
      onRSSISettings();
    } else {
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
          : rssiEnabled
          ? getRSSIColor
            ? getRSSIColor(rssiValue)
            : "primary.main"
          : "text.disabled",
        backgroundColor: !deviceOnline ? "action.disabled" : "action.hover",
        "&:hover": {
          backgroundColor: !deviceOnline
            ? "action.disabled"
            : "action.selected",
        },
      }}
    >
      {rssiEnabled && rssiDisplayMode === "numbers" && rssiValue !== null ? (
        rssiValue
      ) : rssiEnabled ? (
        getRSSIIconComponent(rssiValue)
      ) : (
        <SignalCellularAltIcon />
      )}
    </Fab>
  );

  // Wrap disabled buttons in a span to fix MUI Tooltip warning
  if (!deviceOnline) {
    return (
      <Tooltip title={getRSSIStatusText(rssiValue, rssiEnabled)}>
        <span>{button}</span>
      </Tooltip>
    );
  }

  return (
    <>
      <Tooltip title={getRSSIStatusText(rssiValue, rssiEnabled)}>
        {button}
      </Tooltip>

      {modalOpen && (
        <RSSISettingsModal
          config={config}
          saveConfig={() => {}} // This will be handled by the parent
          saveConfigToDevice={() => {}} // This will be handled by the parent
          deviceOnline={deviceOnline}
          open={modalOpen}
          onClose={handleCloseModal}
          onRSSIDataChange={() => {}} // This will be handled by the parent
        />
      )}
    </>
  );
}
