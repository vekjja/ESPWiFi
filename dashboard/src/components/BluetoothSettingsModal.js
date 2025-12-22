import React, { useState, useEffect, useRef } from "react";
import {
  Box,
  TextField,
  Typography,
  Switch,
  FormControlLabel,
  Button,
  Divider,
  List,
  ListItem,
  ListItemText,
  IconButton,
} from "@mui/material";
import {
  Send as SendIcon,
  Download as DownloadIcon,
  Refresh as RefreshIcon,
  BluetoothDisabled as BluetoothDisabledIcon,
} from "@mui/icons-material";
import SettingsModal from "./SettingsModal";
import SaveButton from "./SaveButton";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";

export default function BluetoothSettingsModal({
  open,
  onClose,
  config,
  saveConfig,
  saveConfigToDevice,
  deviceOnline,
  bluetoothStatus,
  onStatusChange,
}) {
  const [enabled, setEnabled] = useState(config?.bluetooth?.enabled || false);
  const [connected, setConnected] = useState(
    bluetoothStatus?.connected || false
  );
  const [address, setAddress] = useState(bluetoothStatus?.address || "");
  const [loading, setLoading] = useState(false);
  const [filePath, setFilePath] = useState("");
  const [sendFileFs, setSendFileFs] = useState("sd");
  const [receiveFileFs, setReceiveFileFs] = useState("sd");
  const initializedRef = useRef(false);

  // Reset state when modal opens - load current config values
  // Only initialize once when modal opens, don't sync while modal is open
  useEffect(() => {
    if (open && !initializedRef.current) {
      setEnabled(config?.bluetooth?.enabled || false);
      initializedRef.current = true;
      if (onStatusChange) {
        onStatusChange();
      }
    } else if (!open) {
      // Reset the ref when modal closes so it can initialize again next time
      initializedRef.current = false;
    }
    // Only reset when modal opens/closes, not when config or onStatusChange changes
    // This prevents the enabled state from being reset when user toggles it
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  // Update status display when bluetoothStatus prop changes
  useEffect(() => {
    if (bluetoothStatus) {
      setConnected(bluetoothStatus.connected || false);
      setAddress(bluetoothStatus.address || "");
    }
  }, [bluetoothStatus]);

  const handleToggleEnabled = (event) => {
    setEnabled(event.target.checked);
  };

  const handleSave = () => {
    if (!config || !saveConfigToDevice) {
      return;
    }

    const configToSave = {
      ...config,
      bluetooth: {
        ...config.bluetooth,
        enabled: enabled,
      },
    };

    // Save to device (not just local config)
    saveConfigToDevice(configToSave);
    if (onStatusChange) {
      setTimeout(() => onStatusChange(), 500);
    }
    onClose();
  };

  const handleDisconnect = async () => {
    setLoading(true);

    try {
      const response = await fetch(
        buildApiUrl("/api/bluetooth/disconnect"),
        getFetchOptions({
          method: "POST",
        })
      );

      if (!response.ok) {
        throw new Error("Failed to disconnect");
      }

      if (onStatusChange) {
        setTimeout(() => onStatusChange(), 500);
      }
    } catch (err) {
      console.error("Error disconnecting Bluetooth:", err);
    } finally {
      setLoading(false);
    }
  };

  const handleSendFile = async () => {
    if (!filePath.trim() || !connected) {
      return;
    }

    setLoading(true);

    try {
      const url = buildApiUrl(
        `/api/bluetooth/send?fs=${sendFileFs}&path=${encodeURIComponent(
          filePath
        )}`
      );
      const response = await fetch(url, getFetchOptions({ method: "POST" }));

      if (!response.ok) {
        throw new Error("Failed to send file");
      }

      setFilePath(""); // Clear input
    } catch (err) {
      console.error("Error sending file:", err);
    } finally {
      setLoading(false);
    }
  };

  const handleReceiveFile = async () => {
    if (!connected) {
      return;
    }

    setLoading(true);

    try {
      const url = buildApiUrl(
        `/api/bluetooth/receive?fs=${receiveFileFs}&path=/`
      );
      const response = await fetch(url, getFetchOptions());

      if (!response.ok) {
        throw new Error("Failed to receive file");
      }
    } catch (err) {
      console.error("Error receiving file:", err);
    } finally {
      setLoading(false);
    }
  };

  const handleRefresh = () => {
    if (onStatusChange) {
      onStatusChange();
    }
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title="Bluetooth Settings"
      maxWidth="md"
      actions={
        <SaveButton
          onClick={handleSave}
          disabled={!deviceOnline}
          tooltip="Save Settings to Device"
        />
      }
    >
      <Box sx={{ mt: 2 }}>
        {/* Enable/Disable Bluetooth */}
        <FormControlLabel
          control={
            <Switch
              checked={enabled}
              onChange={handleToggleEnabled}
              disabled={loading || !deviceOnline}
            />
          }
          label="Enable Bluetooth"
          sx={{ mb: 2 }}
        />

        {enabled && (
          <>
            <Divider sx={{ my: 2 }} />

            {/* Connection Status */}
            <Box sx={{ mb: 2 }}>
              <Typography variant="subtitle2" gutterBottom>
                Connection Status
              </Typography>
              <List dense>
                <ListItem>
                  <ListItemText
                    primary="Status"
                    secondary={connected ? "Connected" : "Not Connected"}
                  />
                </ListItem>
                {bluetoothStatus?.deviceName && (
                  <ListItem>
                    <ListItemText
                      primary="Device Name"
                      secondary={bluetoothStatus.deviceName}
                    />
                  </ListItem>
                )}
                {address && (
                  <ListItem>
                    <ListItemText primary="MAC Address" secondary={address} />
                  </ListItem>
                )}
              </List>
              {connected && (
                <Button
                  variant="outlined"
                  color="error"
                  startIcon={<BluetoothDisabledIcon />}
                  onClick={handleDisconnect}
                  disabled={loading}
                  sx={{ mt: 1 }}
                >
                  Disconnect
                </Button>
              )}
              <IconButton
                onClick={handleRefresh}
                disabled={loading}
                sx={{ ml: 1 }}
              >
                <RefreshIcon />
              </IconButton>
            </Box>

            <Divider sx={{ my: 2 }} />

            {/* File Sharing - Send */}
            <Box sx={{ mb: 2 }}>
              <Typography variant="subtitle2" gutterBottom>
                Send File via Bluetooth
              </Typography>
              <Box sx={{ display: "flex", gap: 1, mb: 1 }}>
                <TextField
                  select
                  value={sendFileFs}
                  onChange={(e) => setSendFileFs(e.target.value)}
                  SelectProps={{ native: true }}
                  sx={{ minWidth: 100 }}
                >
                  <option value="sd">SD Card</option>
                  <option value="lfs">LittleFS</option>
                </TextField>
                <TextField
                  fullWidth
                  value={filePath}
                  onChange={(e) => setFilePath(e.target.value)}
                  placeholder="/path/to/file"
                  disabled={loading || !connected}
                />
                <Button
                  variant="contained"
                  startIcon={<SendIcon />}
                  onClick={handleSendFile}
                  disabled={loading || !connected || !filePath.trim()}
                >
                  Send
                </Button>
              </Box>
              <Typography variant="caption" color="text.secondary">
                Enter the full path to the file you want to send (e.g.,
                /photos/image.jpg).
              </Typography>
            </Box>

            <Divider sx={{ my: 2 }} />

            {/* File Sharing - Receive */}
            <Box sx={{ mb: 2 }}>
              <Typography variant="subtitle2" gutterBottom>
                Receive File via Bluetooth
              </Typography>
              <Box sx={{ display: "flex", gap: 1, mb: 1 }}>
                <TextField
                  select
                  value={receiveFileFs}
                  onChange={(e) => setReceiveFileFs(e.target.value)}
                  SelectProps={{ native: true }}
                  sx={{ minWidth: 100 }}
                >
                  <option value="sd">SD Card</option>
                  <option value="lfs">LittleFS</option>
                </TextField>
                <Button
                  variant="contained"
                  startIcon={<DownloadIcon />}
                  onClick={handleReceiveFile}
                  disabled={loading || !connected}
                  fullWidth
                >
                  Receive File
                </Button>
              </Box>
              <Typography variant="caption" color="text.secondary">
                Click to receive data from the connected Bluetooth device. Files
                will be saved to the selected filesystem.
              </Typography>
            </Box>

            <Divider sx={{ my: 2 }} />

            <Typography variant="caption" color="text.secondary">
              <strong>Note:</strong> Bluetooth file sharing uses Serial Port
              Profile (SPP). Make sure the connected device supports SPP and is
              properly paired.
            </Typography>
          </>
        )}
      </Box>
    </SettingsModal>
  );
}
