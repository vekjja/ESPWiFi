import React from "react";
import { Box } from "@mui/material";
import DescriptionIcon from "@mui/icons-material/Description";
import SettingsModal from "./SettingsModal";
import DeviceSettingsLogsTab from "./tabPanels/DeviceSettingsLogsTab";

export default function LogsSettingsModal({
  config,
  saveConfigToDevice,
  open = false,
  onClose,
}) {
  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 1,
            width: "100%",
          }}
        >
          <DescriptionIcon color="primary" />
          <span>Logs</span>
        </Box>
      }
      maxWidth={false}
      paperSx={{
        // Better desktop use of space; keep mobile behavior intact
        width: { xs: "90%", sm: "90vw" },
        minWidth: { xs: "90%", sm: "650px" },
        maxWidth: { xs: "90%", sm: "90vw" },
        // Stretch a bit further toward the bottom of the viewport on desktop
        maxHeight: { xs: "90%", sm: "88vh" },
      }}
      // Keep the left rail fixed; only the log output should scroll
      contentSx={{ overflowY: "hidden" }}
    >
      <DeviceSettingsLogsTab
        config={config}
        saveConfigToDevice={saveConfigToDevice}
      />
    </SettingsModal>
  );
}
