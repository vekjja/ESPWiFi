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
import IButton from "./IButton";
import SaveIcon from "@mui/icons-material/SaveAs";

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
    imageRotation = 0,
    size = "medium",
    width = 240,
    height = 240,
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

      {payload === "binary" && (
        <FormControl fullWidth sx={{ marginBottom: 2 }}>
          <InputLabel id="websocket-rotation-select-label">
            Image Rotation
          </InputLabel>
          <Select
            labelId="websocket-rotation-select-label"
            value={imageRotation}
            label="Image Rotation"
            onChange={(e) =>
              onWebSocketDataChange({ imageRotation: e.target.value })
            }
            data-no-dnd="true"
          >
            <MenuItem value={0}>0° (No Rotation)</MenuItem>
            <MenuItem value={90}>90° (Rotate Right)</MenuItem>
            <MenuItem value={180}>180° (Flip)</MenuItem>
            <MenuItem value={270}>270° (Rotate Left)</MenuItem>
          </Select>
        </FormControl>
      )}

      {payload === "text" && (
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
      )}

      <FormControl fullWidth sx={{ marginBottom: 2 }}>
        <InputLabel id="size-select-label">Size</InputLabel>
        <Select
          labelId="size-select-label"
          value={size}
          label="Size"
          onChange={(e) => onWebSocketDataChange({ size: e.target.value })}
          data-no-dnd="true"
        >
          <MenuItem value="small">Small</MenuItem>
          <MenuItem value="medium">Medium</MenuItem>
          <MenuItem value="large">Large</MenuItem>
          <MenuItem value="custom">Custom</MenuItem>
        </Select>
      </FormControl>
      {size === "custom" && (
        <Box sx={{ display: "flex", gap: 2, marginBottom: 2 }}>
          <TextField
            label="Width (px)"
            type="number"
            value={width}
            onChange={(e) =>
              onWebSocketDataChange({ width: Number(e.target.value) })
            }
            variant="outlined"
            sx={{ flex: 1 }}
            data-no-dnd="true"
          />
          <TextField
            label="Height (px)"
            type="number"
            value={height}
            onChange={(e) =>
              onWebSocketDataChange({ height: Number(e.target.value) })
            }
            variant="outlined"
            sx={{ flex: 1 }}
            data-no-dnd="true"
          />
        </Box>
      )}

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
          <IButton
            onClick={handleSave}
            tooltip="Save WebSocket"
            Icon={SaveIcon}
            color="primary"
          />
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
          <IButton
            onClick={handleSave}
            tooltip="Save WebSocket"
            Icon={SaveIcon}
            color="primary"
          />
        </>
      }
    >
      {content}
    </SettingsModal>
  );
}
