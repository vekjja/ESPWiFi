import React from "react";
import {
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  FormControlLabel,
  Checkbox,
  Box,
} from "@mui/material";
import SettingsModal from "./SettingsModal";
import DeleteButton from "./DeleteButton";
import OkayButton from "./OkayButton";

export default function WebSocketSettingsModal({
  open,
  onClose,
  onSave,
  onDelete,
  websocketData,
  onWebSocketDataChange,
  hideModalWrapper = false,
}) {
  const {
    name = "",
    url = "",
    payload = "text",
    fontSize = 14,
    enableSending = true,
  } = websocketData;

  const handleSave = () => {
    onSave();
  };

  const handleDelete = () => {
    onDelete();
  };

  const content = (
    <>
      <TextField
        label="Name"
        value={name}
        onChange={(e) => onWebSocketDataChange({ name: e.target.value })}
        variant="outlined"
        fullWidth
        sx={{ marginBottom: 2 }}
        data-no-dnd="true"
      />

      <TextField
        label="WebSocket URL"
        value={url}
        onChange={(e) => onWebSocketDataChange({ url: e.target.value })}
        variant="outlined"
        fullWidth
        sx={{ marginBottom: 2 }}
        helperText="Use relative path (e.g., /rssi) for same server, or full URL for external WebSockets"
        data-no-dnd="true"
      />

      <FormControl fullWidth sx={{ marginBottom: 2 }}>
        <InputLabel id="websocket-payload-select-label">Payload</InputLabel>
        <Select
          labelId="websocket-payload-select-label"
          value={payload}
          label="Payload"
          onChange={(e) => onWebSocketDataChange({ payload: e.target.value })}
          data-no-dnd="true"
        >
          <MenuItem value="binary">Binary</MenuItem>
          <MenuItem value="text">Text</MenuItem>
        </Select>
      </FormControl>

      <TextField
        label="Font Size (px)"
        type="number"
        value={fontSize}
        onChange={(e) =>
          onWebSocketDataChange({ fontSize: parseInt(e.target.value) || 14 })
        }
        variant="outlined"
        fullWidth
        sx={{ marginBottom: 2 }}
        data-no-dnd="true"
      />

      <FormControlLabel
        control={
          <Checkbox
            checked={enableSending}
            onChange={(e) =>
              onWebSocketDataChange({ enableSending: e.target.checked })
            }
            data-no-dnd="true"
          />
        }
        label="Enable Sending"
      />
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
            <DeleteButton onClick={handleDelete} tooltip="Delete WebSocket" />
          )}
          <OkayButton onClick={handleSave} tooltip="Apply WebSocket Settings" />
        </Box>
      </Box>
    );
  }

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title="WebSocket Settings"
      actions={
        <>
          {onDelete && (
            <DeleteButton onClick={handleDelete} tooltip="Delete WebSocket" />
          )}
          <OkayButton onClick={handleSave} tooltip="Apply WebSocket Settings" />
        </>
      }
    >
      {content}
    </SettingsModal>
  );
}
