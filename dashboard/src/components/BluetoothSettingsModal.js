import React, { useEffect, useState } from "react";
import { Box, Typography } from "@mui/material";
import { Bluetooth as BluetoothIcon } from "@mui/icons-material";
import SettingsModal from "./SettingsModal";

export default function BluetoothSettingsModal({
  open,
  onClose,
  config,
  saveConfig,
  saveConfigToDevice,
  deviceOnline,
}) {
  const [enabled, setEnabled] = useState(config?.bluetooth?.enabled || false);

  useEffect(() => {
    if (open) {
      setEnabled(config?.bluetooth?.enabled || false);
    }
  }, [open, config?.bluetooth?.enabled]);

  const handleToggle = async () => {
    if (!deviceOnline) return;
    const next = !enabled;
    setEnabled(next);
    const updated = {
      ...(config || {}),
      bluetooth: {
        ...(config?.bluetooth || {}),
        enabled: next,
      },
    };
    saveConfig?.(updated);
    if (saveConfigToDevice) {
      await Promise.resolve(saveConfigToDevice(updated));
    }
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      maxWidth={false}
      fullWidth={false}
      paperSx={{
        width: { xs: "99%", sm: "63vw" },
        maxWidth: { xs: "99%", sm: "63vw" },
        height: { xs: "90%", sm: "63vh" },
        maxHeight: { xs: "99%", sm: "63vh" },
      }}
      title={
        <span
          role="button"
          tabIndex={deviceOnline ? 0 : -1}
          aria-pressed={enabled}
          onClick={handleToggle}
          onKeyDown={(e) => {
            if (e.key === "Enter" || e.key === " ") {
              e.preventDefault();
              handleToggle();
            }
          }}
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: "8px",
            width: "100%",
          }}
        >
          <BluetoothIcon color={enabled ? "primary" : "disabled"} />
          Bluetooth
        </span>
      }
    >
      <Box sx={{ px: 2, py: 1 }}>
        <Typography variant="body2" color="text.secondary">
          {enabled ? "Bluetooth Enabled" : "Bluetooth Disabled"}
        </Typography>
      </Box>
    </SettingsModal>
  );
}
