import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import { Add as AddIcon } from "@mui/icons-material";
import AddModuleModal from "./AddModuleModal";

export default function AddModuleButton({
  config,
  deviceOnline,
  onAddModule,
  saveConfig,
  saveConfigToDevice,
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
          <AddIcon />
        </Fab>
      </Tooltip>

      {modalOpen && (
        <AddModuleModal
          open={modalOpen}
          onClose={handleCloseModal}
          config={config}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
        />
      )}
    </>
  );
}
