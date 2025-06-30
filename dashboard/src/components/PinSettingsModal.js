import React from "react";
import {
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  FormControlLabel,
  Checkbox,
  Tooltip,
  Box,
} from "@mui/material";
import SettingsModal from "./SettingsModal";
import DeleteButton from "./DeleteButton";
import OkayButton from "./OkayButton";

export default function PinSettingsModal({
  open,
  onClose,
  onSave,
  onDelete,
  pinData,
  onPinDataChange,
  hideModalWrapper = false,
}) {
  const {
    name = "",
    pinNumber = "",
    mode = "out",
    inverted = false,
    remoteURL = "",
    dutyMin = 0,
    dutyMax = 255,
  } = pinData;

  const handleSave = () => {
    onSave();
  };

  const handleDelete = () => {
    onDelete();
  };

  const content = (
    <>
      <FormControlLabel
        control={
          <Tooltip title="Invert: When checked, the pin will be LOW when the UI is ON">
            <Checkbox
              checked={inverted}
              onChange={(e) => onPinDataChange({ inverted: e.target.checked })}
              data-no-dnd="true"
            />
          </Tooltip>
        }
        label="Invert"
        sx={{ marginBottom: 2 }}
      />

      <TextField
        label="Name"
        value={name}
        onChange={(e) => onPinDataChange({ name: e.target.value })}
        variant="outlined"
        fullWidth
        sx={{ marginBottom: 2 }}
        data-no-dnd="true"
      />

      <TextField
        label="Pin Number"
        value={pinNumber}
        onChange={(e) => onPinDataChange({ pinNumber: e.target.value })}
        variant="outlined"
        fullWidth
        sx={{ marginBottom: 2 }}
        data-no-dnd="true"
      />

      <FormControl fullWidth variant="outlined" sx={{ marginBottom: 2 }}>
        <InputLabel id="mode-select-label">Mode</InputLabel>
        <Select
          labelId="mode-select-label"
          value={mode}
          onChange={(e) => onPinDataChange({ mode: e.target.value })}
          data-no-dnd="true"
        >
          <MenuItem value="in">Input</MenuItem>
          <MenuItem value="out">Output</MenuItem>
          <MenuItem value="pwm">PWM</MenuItem>
        </Select>
      </FormControl>

      <TextField
        label="Remote GPIO URL (optional)"
        value={remoteURL}
        onChange={(e) => onPinDataChange({ remoteURL: e.target.value })}
        variant="outlined"
        fullWidth
        placeholder="http://192.168.1.100 or esp32.local"
        helperText="Leave empty to use local ESP32. Include protocol (http://) for remote devices."
        sx={{ marginBottom: 2 }}
        data-no-dnd="true"
      />

      {mode === "pwm" && (
        <>
          <TextField
            label="Min Duty (µs)"
            value={dutyMin}
            onChange={(e) => onPinDataChange({ dutyMin: e.target.value })}
            variant="outlined"
            fullWidth
            sx={{ marginBottom: 2 }}
            data-no-dnd="true"
          />
          <TextField
            label="Max Duty (µs)"
            value={dutyMax}
            onChange={(e) => onPinDataChange({ dutyMax: e.target.value })}
            variant="outlined"
            fullWidth
            sx={{ marginBottom: 2 }}
            data-no-dnd="true"
          />
        </>
      )}
    </>
  );

  if (hideModalWrapper) {
    return (
      <Box sx={{ p: 3 }}>
        {content}
        <Box
          sx={{ display: "flex", justifyContent: "flex-end", gap: 1, mt: 2 }}
        >
          {onDelete && (
            <DeleteButton onClick={handleDelete} tooltip="Delete Pin" />
          )}
          <OkayButton onClick={handleSave} tooltip="Apply Pin Settings" />
        </Box>
      </Box>
    );
  }

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title="Pin Settings"
      actions={
        <>
          {onDelete && (
            <DeleteButton onClick={handleDelete} tooltip="Delete Pin" />
          )}
          <OkayButton onClick={handleSave} tooltip="Apply Pin Settings" />
        </>
      }
    >
      {content}
    </SettingsModal>
  );
}
