import React from "react";
import { Typography, Box } from "@mui/material";
import SignalCellularAltIcon from "@mui/icons-material/SignalCellularAlt";
import SettingsModal from "./SettingsModal";

export default function RSSISettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
  rssiValue = null,
}) {
  const handleCloseModal = () => {
    if (onClose) onClose();
  };

  return (
    <SettingsModal
      open={open}
      onClose={handleCloseModal}
      title={
        <span
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: "8px",
            width: "100%",
          }}
        >
          <SignalCellularAltIcon color="primary" />
          RSSI {rssiValue !== null ? `${rssiValue} dBm` : "N/A"}
        </span>
      }
      actions={null}
    >
      <Box
        sx={{
          marginTop: 2,
          padding: 2,
          backgroundColor: "rgba(71, 255, 240, 0.1)",
          borderRadius: 1,
        }}
      >
        <Typography variant="body2" color="primary.main">
          📶{" "}
          <a
            href="https://en.wikipedia.org/wiki/Received_signal_strength_indication"
            target="_blank"
            rel="noopener noreferrer"
            style={{ color: "inherit", textDecoration: "underline" }}
          >
            Received Signal Strength Indicator (RSSI)
          </a>
        </Typography>
      </Box>
    </SettingsModal>
  );
}
