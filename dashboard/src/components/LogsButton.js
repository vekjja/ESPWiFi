import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import DescriptionIcon from "@mui/icons-material/Description";
import LogsSettingsModal from "./LogsSettingsModal";

export default function LogsButton({
  config,
  deviceOnline,
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
      <Tooltip title="Logs">
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
            <DescriptionIcon />
          </Fab>
        </span>
      </Tooltip>

      {modalOpen && (
        <LogsSettingsModal
          open={modalOpen}
          onClose={handleCloseModal}
          config={config}
          saveConfigToDevice={saveConfigToDevice}
        />
      )}
    </>
  );
}
