import React, { useState } from "react";
import { Fab, Tooltip, Dialog, DialogContent, IconButton } from "@mui/material";
import BluetoothSearchingIcon from "@mui/icons-material/BluetoothSearching";
import CloseIcon from "@mui/icons-material/Close";
import BlePairingFlow from "./BlePairingFlow";

export default function BluetoothButton({
  config,
  deviceOnline,
  onDevicePaired,
}) {
  const [modalOpen, setModalOpen] = useState(false);

  const handleClick = () => {
    setModalOpen(true);
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  const handleDevicePaired = (deviceRecord, details) => {
    setModalOpen(false);
    onDevicePaired?.(deviceRecord, details);
  };

  const isDisabled = !config;

  return (
    <>
      <Tooltip title={isDisabled ? (!config ? "Loading configuration..." : "Control socket disconnected") : "Pair BLE Device"}>
        <Fab
          size="medium"
          color="primary"
          onClick={handleClick}
          disabled={isDisabled}
          sx={{
            color: isDisabled ? "text.disabled" : "primary.main",
            backgroundColor: isDisabled ? "action.disabled" : "action.hover",
            "&:hover": {
              backgroundColor: isDisabled
                ? "action.disabled"
                : "action.selected",
            },
          }}
        >
          <BluetoothSearchingIcon />
        </Fab>
      </Tooltip>

      <Dialog
        open={modalOpen}
        onClose={handleCloseModal}
        fullScreen
        sx={{
          "& .MuiDialog-paper": {
            backgroundColor: "background.default",
          },
        }}
      >
        <IconButton
          onClick={handleCloseModal}
          sx={{
            position: "absolute",
            right: 16,
            top: 16,
            zIndex: 1,
            color: "text.secondary",
          }}
        >
          <CloseIcon />
        </IconButton>
        <DialogContent
          sx={{
            p: 0,
            display: "flex",
            flexDirection: "column",
            overflow: "auto",
          }}
        >
          <BlePairingFlow
            onDeviceProvisioned={handleDevicePaired}
            onClose={handleCloseModal}
          />
        </DialogContent>
      </Dialog>
    </>
  );
}
