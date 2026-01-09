import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import AddIcon from "@mui/icons-material/Add";
import AddModuleModal from "./AddModuleModal";

export default function AddModuleButton({
  config,
  deviceOnline,
  onAddModule,
  saveConfig,
  saveConfigToDevice,
  missingSettingsButtons = [],
  onAddSettingsButton,
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
      <Tooltip title="Add Module">
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
            <AddIcon />
          </Fab>
        </span>
      </Tooltip>

      {modalOpen && (
        <AddModuleModal
          open={modalOpen}
          onClose={handleCloseModal}
          config={config}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          missingSettingsButtons={missingSettingsButtons}
          onAddSettingsButton={onAddSettingsButton}
        />
      )}
    </>
  );
}
