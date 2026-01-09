import React, { useState } from "react";
import { Fab, Tooltip } from "@mui/material";
import DevicesIcon from "@mui/icons-material/Devices";
import DevicePickerDialog from "./DevicePickerDialog";

export default function DevicePickerButton({
  devices,
  selectedId,
  onSelectDevice,
  onRemoveDevice,
  onPairNew,
}) {
  const [open, setOpen] = useState(false);

  const handleOpen = () => setOpen(true);
  const handleClose = () => setOpen(false);

  return (
    <>
      <Tooltip title="Devices">
        <Fab
          size="medium"
          color="primary"
          onClick={handleOpen}
          sx={{
            color: "primary.main",
            backgroundColor: "action.hover",
            "&:hover": { backgroundColor: "action.selected" },
          }}
        >
          <DevicesIcon />
        </Fab>
      </Tooltip>

      {open && (
        <DevicePickerDialog
          open={open}
          onClose={handleClose}
          devices={devices}
          selectedId={selectedId}
          onSelect={(d) => {
            onSelectDevice?.(d);
            handleClose();
          }}
          onRemove={(d) => onRemoveDevice?.(d)}
          onPairNew={() => {
            handleClose();
            onPairNew?.();
          }}
        />
      )}
    </>
  );
}
