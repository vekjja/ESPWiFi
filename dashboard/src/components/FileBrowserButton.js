import React, { useState } from "react";
import { Container, Fab, Tooltip } from "@mui/material";
import { FolderOpen } from "@mui/icons-material";
import SettingsModal from "./SettingsModal";
import FileBrowserComponent from "./FileBrowser";

export default function FileBrowserButton({
  config,
  deviceOnline = true,
  open = false,
  onClose,
}) {
  const [isFileBrowserOpen, setIsFileBrowserOpen] = useState(false);

  const handleOpenFileBrowser = () => {
    setIsFileBrowserOpen(true);
  };

  const handleCloseFileBrowser = () => {
    setIsFileBrowserOpen(false);
    if (onClose) onClose();
  };

  // Use external open prop if provided, otherwise use internal state
  const modalOpen = open !== undefined ? open : isFileBrowserOpen;
  const handleModalClose =
    open !== undefined ? onClose : handleCloseFileBrowser;

  return (
    <SettingsModal
      open={modalOpen}
      onClose={handleModalClose}
      title="File Browser"
      maxWidth="lg"
      fullWidth
    >
      <FileBrowserComponent config={config} deviceOnline={deviceOnline} />
    </SettingsModal>
  );
}
