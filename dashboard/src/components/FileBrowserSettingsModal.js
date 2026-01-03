import React from "react";
import { Box } from "@mui/material";
import { FolderOpen as FolderOpenIcon } from "@mui/icons-material";
import SettingsModal from "./SettingsModal";
import FileBrowser from "./FileBrowser";

export default function FileBrowserSettingsModal({
  config,
  deviceOnline,
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
          <FolderOpenIcon color="primary" />
          <span>File Browser</span>
        </Box>
      }
      maxWidth={false}
      fullWidth={false}
      paperSx={{
        // Better desktop use of space; keep mobile behavior intact
        width: { xs: "99%", sm: "99vw" },
        minWidth: { xs: "90%", sm: "650px" },
        maxWidth: { xs: "90%", sm: "95vw" },
        height: { xs: "90%", sm: "88vh" },
        maxHeight: { xs: "90%", sm: "88vh" },
      }}
      contentSx={{
        overflowY: "hidden",
        p: { xs: 2, sm: 1.5 },
        pt: { xs: 1.5, sm: 1.25 },
        pb: { xs: 1.5, sm: 1.25 },
      }}
    >
      <FileBrowser config={config} deviceOnline={deviceOnline} />
    </SettingsModal>
  );
}


