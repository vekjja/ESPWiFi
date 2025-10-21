import React, { useState } from "react";
import { Container, Fab, Tooltip } from "@mui/material";
import { FolderOpen } from "@mui/icons-material";
import SettingsModal from "./SettingsModal";
import FileBrowserComponent from "./FileBrowser";

export default function FileBrowserButton({ config, deviceOnline = true }) {
  const [isFileBrowserOpen, setIsFileBrowserOpen] = useState(false);

  const handleOpenFileBrowser = () => {
    setIsFileBrowserOpen(true);
  };

  const handleCloseFileBrowser = () => {
    setIsFileBrowserOpen(false);
  };

  return (
    <Container
      sx={{
        display: "flex",
        flexWrap: "wrap",
        justifyContent: "center",
      }}
    >
      {/* File Browser Button */}
      <Tooltip title="File Browser - Browse SD card and LittleFS files">
        <Fab
          size="small"
          color="primary"
          aria-label="file-browser"
          onClick={handleOpenFileBrowser}
          sx={{
            position: "fixed",
            top: "20px",
            left: "200px", // Position next to RSSI button
            color: deviceOnline ? "primary.main" : "text.disabled",
            backgroundColor: deviceOnline ? "action.hover" : "action.disabled",
            "&:hover": {
              backgroundColor: deviceOnline
                ? "action.selected"
                : "action.disabledBackground",
            },
          }}
        >
          <FolderOpen />
        </Fab>
      </Tooltip>

      {/* File Browser Modal */}
      <SettingsModal
        open={isFileBrowserOpen}
        onClose={handleCloseFileBrowser}
        title="File Browser"
        maxWidth="lg"
        fullWidth
      >
        <FileBrowserComponent config={config} deviceOnline={deviceOnline} />
      </SettingsModal>
    </Container>
  );
}
