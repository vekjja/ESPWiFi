import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import { FolderOpen as FolderOpenIcon } from "@mui/icons-material";
import SettingsModal from "./SettingsModal";
import FileBrowserComponent from "./FileBrowser";

export default function FileBrowserButton({
  config,
  deviceOnline,
  onFileBrowser,
}) {
  const [modalOpen, setModalOpen] = useState(false);

  const handleClick = () => {
    if (deviceOnline) {
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  return (
    <>
      <Tooltip title="File Browser - Browse device files">
        <Fab
          size="medium"
          color="primary"
          onClick={handleClick}
          disabled={!deviceOnline}
          sx={{
            color: !deviceOnline ? "text.disabled" : "primary.main",
            backgroundColor: !deviceOnline ? "action.disabled" : "action.hover",
            "&:hover": {
              backgroundColor: !deviceOnline
                ? "action.disabled"
                : "action.selected",
            },
          }}
        >
          <FolderOpenIcon />
        </Fab>
      </Tooltip>

      {modalOpen && (
        <SettingsModal
          open={modalOpen}
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
              <FolderOpenIcon color="primary" />
              File Browser
            </span>
          }
          maxWidth={false}
          fullWidth={false}
        >
          <FileBrowserComponent config={config} deviceOnline={deviceOnline} />
        </SettingsModal>
      )}
    </>
  );
}
