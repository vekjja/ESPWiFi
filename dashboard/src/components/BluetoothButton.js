import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import BluetoothSearchingIcon from "@mui/icons-material/BluetoothSearching";
import BluetoothSettingsModal from "./BluetoothSettingsModal";

export default function BluetoothButton({ config, onDevicePaired }) {
  const [modalOpen, setModalOpen] = useState(false);

  const handleClick = () => {
    setModalOpen(true);
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  return (
    <>
      <Tooltip title="Bluetooth Settings">
        <span>
          <Fab
            size="medium"
            color="primary"
            onClick={handleClick}
            sx={{
              color: "primary.main",
              backgroundColor: "action.hover",
              "&:hover": {
                backgroundColor: "action.selected",
              },
            }}
          >
            <BluetoothSearchingIcon />
          </Fab>
        </span>
      </Tooltip>

      {modalOpen && (
        <BluetoothSettingsModal
          open={modalOpen}
          onClose={handleCloseModal}
          config={config}
          saveConfig={() => {}}
          saveConfigToDevice={() => {}}
          deviceOnline={Boolean(config)}
        />
      )}
    </>
  );
}
