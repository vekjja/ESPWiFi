import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import FolderOpenIcon from "@mui/icons-material/FolderOpen";
import SettingsModal from "./SettingsModal";
import FileBrowserComponent from "./FileBrowser";

export default function FileBrowserButton({
  config,
  deviceOnline,
  controlWs,
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
        {/* MUI Tooltip cannot attach events to a disabled button; wrap in span */}
        <span>
          <Fab
            size="medium"
            color="primary"
            onClick={handleClick}
            disabled={!deviceOnline}
            sx={{
              color: !deviceOnline ? "text.disabled" : "primary.main",
              backgroundColor: !deviceOnline
                ? "action.disabled"
                : "action.hover",
              "&:hover": {
                backgroundColor: !deviceOnline
                  ? "action.disabled"
                  : "action.selected",
              },
            }}
          >
            <FolderOpenIcon />
          </Fab>
        </span>
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
          paperSx={{
            width: { xs: "99%", sm: "63vw" },
            maxWidth: { xs: "99%", sm: "95vw" },
            height: { xs: "90%", sm: "90vh" },
            maxHeight: { xs: "99%", sm: "99vh" },
          }}
          contentSx={{
            overflowY: "hidden",
            p: { xs: 2, sm: 1.5 },
            pt: { xs: 1.5, sm: 1.25 },
            pb: { xs: 1.5, sm: 1.25 },
          }}
        >
          <FileBrowserComponent
            config={config}
            deviceOnline={deviceOnline}
            controlWs={controlWs}
          />
        </SettingsModal>
      )}
    </>
  );
}
