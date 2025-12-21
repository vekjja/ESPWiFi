import React, { useState, useEffect } from "react";
import {
  Box,
  TextField,
  Typography,
  Switch,
  FormControlLabel,
  Button,
  Divider,
  Alert,
  CircularProgress,
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
  const [enabled, setEnabled] = useState(bluetoothStatus?.enabled || false);
  const [deviceName, setDeviceName] = useState(
    bluetoothStatus?.deviceName || config?.bluetooth?.deviceName || ""
  );
  const [connected, setConnected] = useState(
    bluetoothStatus?.connected || false
  );
  const [address, setAddress] = useState(bluetoothStatus?.address || "");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [success, setSuccess] = useState(null);
  const [filePath, setFilePath] = useState("");
  const [sendFileFs, setSendFileFs] = useState("sd");
  const [receiveFileFs, setReceiveFileFs] = useState("sd");

  // Update state when bluetoothStatus prop changes
  useEffect(() => {
    if (bluetoothStatus) {
      setEnabled(bluetoothStatus.enabled || false);
      setDeviceName(
        bluetoothStatus.deviceName || config?.bluetooth?.deviceName || ""
      );
      setConnected(bluetoothStatus.connected || false);
      setAddress(bluetoothStatus.address || "");
    }
  }, [bluetoothStatus, config]);

  // Reset state when modal opens
  useEffect(() => {
    if (open && onStatusChange) {
      onStatusChange();
    }
  }, [open, onStatusChange]);

  const handleToggleEnabled = async (event) => {
    const newEnabled = event.target.checked;
    setEnabled(newEnabled);
    setLoading(true);
    setError(null);
    setSuccess(null);

    try {
      const response = await fetch(
        buildApiUrl("/api/bluetooth/enable"),
        getFetchOptions({
          method: "POST",
          body: JSON.stringify({ enabled: newEnabled }),
        })
      );

      if (!response.ok) {
        const errorData = await response.json();
        throw new Error(errorData.error || "Failed to update Bluetooth status");
      }

      setSuccess(`Bluetooth ${newEnabled ? "enabled" : "disabled"}`);
      if (onStatusChange) {
        setTimeout(() => onStatusChange(), 500);
      }
    } catch (err) {
      setError(err.message);
      setEnabled(!newEnabled); // Revert on error
    } finally {
      setLoading(false);
    }
  };

  const handleSaveDeviceName = async () => {
    if (!deviceName.trim()) {
      setError("Device name cannot be empty");
      return;
    }

    setLoading(true);
    setError(null);
    setSuccess(null);

    try {
      const response = await fetch(
        buildApiUrl("/api/bluetooth/name"),
        getFetchOptions({
          method: "POST",
          body: JSON.stringify({ deviceName: deviceName.trim() }),
        })
      );

      if (!response.ok) {
        const errorData = await response.json();
        throw new Error(errorData.error || "Failed to update device name");
      }

      setSuccess("Device name updated successfully");
      if (onStatusChange) {
        setTimeout(() => onStatusChange(), 500);
      }

      // Update config
      if (saveConfig && config) {
        const updatedConfig = {
          ...config,
          bluetooth: {
            ...config.bluetooth,
            deviceName: deviceName.trim(),
          },
        };
        saveConfig(updatedConfig);
      }
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleDisconnect = async () => {
    setLoading(true);
    setError(null);
    setSuccess(null);

    try {
      const response = await fetch(
        buildApiUrl("/api/bluetooth/disconnect"),
        getFetchOptions({
          method: "POST",
        })
      );

      if (!response.ok) {
        const errorData = await response.json();
        throw new Error(errorData.error || "Failed to disconnect");
      }

      setSuccess("Disconnected successfully");
      if (onStatusChange) {
        setTimeout(() => onStatusChange(), 500);
      }
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleSendFile = async () => {
    if (!filePath.trim()) {
      setError("Please enter a file path");
      return;
    }

    if (!connected) {
      setError("Bluetooth must be connected to send files");
      return;
    }

    setLoading(true);
    setError(null);
    setSuccess(null);

    try {
      const url = buildApiUrl(
        `/api/bluetooth/send?fs=${sendFileFs}&path=${encodeURIComponent(
          filePath
        )}`
      );
      const response = await fetch(url, getFetchOptions({ method: "POST" }));

      if (!response.ok) {
        const errorData = await response.json();
        throw new Error(errorData.error || "Failed to send file");
      }

      const data = await response.json();
      setSuccess(`File sent successfully (${data.bytesSent} bytes)`);
      setFilePath(""); // Clear input
    } catch (err) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleReceiveFile = async () => {
    if (!connected) {
      setError("Bluetooth must be connected to receive files");
      return;
    }

    setLoading(true);
    setError(null);
    setSuccess(null);

    try {
      const url = buildApiUrl(
        `/api/bluetooth/receive?fs=${receiveFileFs}&path=/`
      );
      const response = await fetch(url, getFetchOptions());

      if (!response.ok) {
        const errorData = await response.json();
        throw new Error(errorData.error || "Failed to receive file");
      }

      const data = await response.json();
      setSuccess(
        `File received successfully: ${data.filePath} (${data.bytesReceived} bytes)`
      );
    } catch (err) {
      setError(err.message);
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
        <Box sx={{ display: "flex", gap: 1, alignItems: "center" }}>
          {loading && <CircularProgress size={24} />}
          <SaveButton
            onClick={handleSaveDeviceName}
            disabled={loading || !enabled || !deviceOnline}
            label="Save Name"
          />
        </Box>
      }
    >
      <Box sx={{ mt: 2 }}>
        {error && (
          <Alert severity="error" sx={{ mb: 2 }} onClose={() => setError(null)}>
            {error}
          </Alert>
        )}
        {success && (
          <Alert
            severity="success"
            sx={{ mb: 2 }}
            onClose={() => setSuccess(null)}
          >
            {success}
          </Alert>
        )}

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
                {deviceName && (
                  <ListItem>
                    <ListItemText
                      primary="Device Name"
                      secondary={deviceName}
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

            {/* Device Name */}
            <Box sx={{ mb: 2 }}>
              <Typography variant="subtitle2" gutterBottom>
                Device Name
              </Typography>
              <TextField
                fullWidth
                value={deviceName}
                onChange={(e) => setDeviceName(e.target.value)}
                placeholder="Enter Bluetooth device name"
                disabled={loading || !deviceOnline}
                sx={{ mb: 1 }}
              />
              <Typography variant="caption" color="text.secondary">
                This name will be visible to other devices when scanning for
                Bluetooth devices.
              </Typography>
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
