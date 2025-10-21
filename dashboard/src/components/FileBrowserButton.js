import React, { useState } from "react";
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
          <FolderOpen color="primary" />
          File Browser
        </span>
      }
      maxWidth={false}
      fullWidth={false}
    >
      <FileBrowserComponent config={config} deviceOnline={deviceOnline} />
    </SettingsModal>
  );
}
