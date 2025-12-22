import React, { useState, useEffect, useRef, useCallback } from "react";
import {
  Box,
  Typography,
  Button,
  Divider,
  List,
  ListItem,
  ListItemText,
  Paper,
  Grid,
  Switch,
  FormControlLabel,
  MenuItem,
  Select,
  FormControl,
  InputLabel,
  ToggleButtonGroup,
  ToggleButton,
  Alert,
  CircularProgress,
} from "@mui/material";
import {
  BluetoothDisabled as BluetoothDisabledIcon,
  Bluetooth as BluetoothIcon,
  Send as SendIcon,
  GetApp as GetAppIcon,
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
}) {
  const [loading, setLoading] = useState(false);
  const [enabled, setEnabled] = useState(config?.bluetooth?.enabled || false);
  const initializedRef = useRef(false);
  const [fileSystem, setFileSystem] = useState("sd");
  const [files, setFiles] = useState([]);
  const [loadingFiles, setLoadingFiles] = useState(false);
  const [selectedFile, setSelectedFile] = useState("");
  const [sendLoading, setSendLoading] = useState(false);
  const [receiveLoading, setReceiveLoading] = useState(false);
  const [fileError, setFileError] = useState(null);

  // Recursively fetch all files from the filesystem
  const fetchAllFiles = useCallback(
    async (path = "/", fs = fileSystem, allFiles = []) => {
      try {
        const response = await fetch(
          buildApiUrl(`/api/files?fs=${fs}&path=${encodeURIComponent(path)}`),
          getFetchOptions()
        );

        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const data = await response.json();
        const fileList = data.files || [];

        for (const file of fileList) {
          if (file.isDirectory) {
            // Recursively fetch files from subdirectories
            await fetchAllFiles(file.path, fs, allFiles);
          } else {
            // Add file to list with filesystem prefix
            allFiles.push({
              path: file.path,
              name: file.name,
              size: file.size,
              display: `${fs}:${file.path}`,
              fs: fs,
            });
          }
        }

        return allFiles;
      } catch (error) {
        console.error("Error fetching files:", error);
        throw error;
      }
    },
    [fileSystem]
  );

  // Load files when modal opens and Bluetooth is enabled
  useEffect(() => {
    if (open && enabled && deviceOnline && !loadingFiles) {
      setLoadingFiles(true);
      setFileError(null);
      fetchAllFiles("/", fileSystem, [])
        .then((fileList) => {
          setFiles(fileList);
        })
        .catch((err) => {
          setFileError(err.message);
          setFiles([]);
        })
        .finally(() => {
          setLoadingFiles(false);
        });
    }
  }, [open, enabled, deviceOnline, fileSystem, fetchAllFiles, loadingFiles]);

  // Reset state when modal opens - load current config values
  // Only initialize once when modal opens, don't sync while modal is open
  useEffect(() => {
    if (open && !initializedRef.current) {
      setEnabled(config?.bluetooth?.enabled || false);
      initializedRef.current = true;
    } else if (!open) {
      // Reset the ref when modal closes so it can initialize again next time
      initializedRef.current = false;
      setSelectedFile("");
      setFileError(null);
    }
    // Only reset when modal opens/closes, not when config changes
    // This prevents the enabled state from being reset when user toggles it
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

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
    } catch (err) {
      console.error("Error disconnecting Bluetooth:", err);
    } finally {
      setLoading(false);
    }
  };

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
    onClose();
  };

  const handleSendFile = async () => {
    if (!selectedFile) {
      setFileError("Please select a file to send");
      return;
    }

    const file = files.find((f) => f.display === selectedFile);
    if (!file) {
      setFileError("Selected file not found");
      return;
    }

    setSendLoading(true);
    setFileError(null);

    try {
      const response = await fetch(
        buildApiUrl(
          `/api/bluetooth/send?fs=${file.fs}&path=${encodeURIComponent(
            file.path
          )}`
        ),
        getFetchOptions({
          method: "POST",
        })
      );

      if (!response.ok) {
        const errorData = await response.json().catch(() => ({}));
        throw new Error(errorData.error || "Failed to send file");
      }

      await response.json();
      // Success - file sent
      setFileError(null);
    } catch (err) {
      setFileError(err.message || "Failed to send file");
      console.error("Error sending file:", err);
    } finally {
      setSendLoading(false);
    }
  };

  const handleReceiveFile = async () => {
    setReceiveLoading(true);
    setFileError(null);

    try {
      const response = await fetch(
        buildApiUrl("/api/bluetooth/receive?fs=sd&path=/"),
        getFetchOptions({
          method: "GET",
        })
      );

      if (!response.ok) {
        const errorData = await response.json().catch(() => ({}));
        throw new Error(errorData.error || "Failed to receive file");
      }

      await response.json();
      // Success - file received and saved
      setFileError(null);
      // Refresh file list to show new file
      const fileList = await fetchAllFiles("/", fileSystem, []);
      setFiles(fileList);
    } catch (err) {
      setFileError(err.message || "Failed to receive file");
      console.error("Error receiving file:", err);
    } finally {
      setReceiveLoading(false);
    }
  };
  const connected = config?.bluetooth?.connected || false;
  const deviceName = config?.deviceName || "";
  const address = config?.bluetooth?.address || "";
  const connectionCount = config?.bluetooth?.connectionCount || 0;

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 1,
          }}
        >
          <BluetoothIcon sx={{ color: "primary.main" }} />
          Bluetooth Settings
        </Box>
      }
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

        {!enabled ? (
          <Paper
            sx={{ p: 3, textAlign: "center", bgcolor: "background.default" }}
          >
            <Typography variant="body1" color="text.secondary">
              Bluetooth is disabled
            </Typography>
            <Typography
              variant="caption"
              color="text.secondary"
              sx={{ mt: 1, display: "block" }}
            >
              Enable Bluetooth to view status information
            </Typography>
          </Paper>
        ) : (
          <>
            {/* Connection Status */}
            <Box sx={{ mb: 3 }}>
              <List dense>
                <ListItem>
                  <ListItemText
                    primary="Status"
                    secondary={
                      <Typography
                        variant="body2"
                        color={connected ? "success.main" : "text.secondary"}
                        sx={{ fontWeight: connected ? 500 : 400 }}
                      >
                        {connected ? "Connected" : "Not Connected"}
                      </Typography>
                    }
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
                {config?.bluetooth?.installed !== undefined && (
                  <ListItem>
                    <ListItemText
                      primary="Bluetooth Support"
                      secondary={
                        config.bluetooth.installed
                          ? "Installed"
                          : "Not Available"
                      }
                    />
                  </ListItem>
                )}
                {connectionCount > 0 && (
                  <ListItem>
                    <ListItemText
                      primary="Active Connections"
                      secondary={`${connectionCount} device${
                        connectionCount > 1 ? "s" : ""
                      }`}
                    />
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
                  sx={{ mt: 2 }}
                >
                  Disconnect
                </Button>
              )}
            </Box>

            <Divider sx={{ my: 3 }} />

            <Divider sx={{ my: 3 }} />

            {/* File Transfer */}
            {connected && (
              <Box sx={{ mb: 3 }}>
                <Typography variant="h6" gutterBottom>
                  File Transfer
                </Typography>

                {fileError && (
                  <Alert severity="error" sx={{ mb: 2 }}>
                    {fileError}
                  </Alert>
                )}

                {/* Filesystem Selector */}
                <FormControl fullWidth sx={{ mb: 2 }}>
                  <ToggleButtonGroup
                    value={fileSystem}
                    exclusive
                    onChange={(e, newFs) => {
                      if (newFs !== null) {
                        setFileSystem(newFs);
                        setSelectedFile("");
                      }
                    }}
                    aria-label="file system"
                  >
                    <ToggleButton value="sd" aria-label="SD card">
                      SD Card
                    </ToggleButton>
                    <ToggleButton value="lfs" aria-label="LittleFS">
                      LittleFS
                    </ToggleButton>
                  </ToggleButtonGroup>
                </FormControl>

                {/* Send File */}
                <Box sx={{ mb: 3 }}>
                  <Typography variant="subtitle2" gutterBottom>
                    Send File to Bluetooth Device
                  </Typography>
                  <FormControl fullWidth sx={{ mb: 2 }}>
                    <InputLabel>Select File</InputLabel>
                    <Select
                      value={selectedFile}
                      onChange={(e) => setSelectedFile(e.target.value)}
                      label="Select File"
                      disabled={loadingFiles || sendLoading}
                    >
                      {loadingFiles ? (
                        <MenuItem disabled>
                          <CircularProgress size={20} sx={{ mr: 1 }} />
                          Loading files...
                        </MenuItem>
                      ) : files.length === 0 ? (
                        <MenuItem disabled>No files available</MenuItem>
                      ) : (
                        files.map((file) => (
                          <MenuItem key={file.display} value={file.display}>
                            {file.display} ({file.size} bytes)
                          </MenuItem>
                        ))
                      )}
                    </Select>
                  </FormControl>
                  <Button
                    variant="contained"
                    color="primary"
                    startIcon={
                      sendLoading ? (
                        <CircularProgress size={16} />
                      ) : (
                        <SendIcon />
                      )
                    }
                    onClick={handleSendFile}
                    disabled={!selectedFile || sendLoading || loadingFiles}
                    fullWidth
                  >
                    {sendLoading ? "Sending..." : "Send File"}
                  </Button>
                </Box>

                <Divider sx={{ my: 2 }} />

                {/* Receive File */}
                <Box>
                  <Typography variant="subtitle2" gutterBottom>
                    Receive File from Bluetooth Device
                  </Typography>
                  <Button
                    variant="contained"
                    color="secondary"
                    startIcon={
                      receiveLoading ? (
                        <CircularProgress size={16} />
                      ) : (
                        <GetAppIcon />
                      )
                    }
                    onClick={handleReceiveFile}
                    disabled={receiveLoading}
                    fullWidth
                  >
                    {receiveLoading ? "Receiving..." : "Receive File"}
                  </Button>
                </Box>
              </Box>
            )}

            <Divider sx={{ my: 3 }} />

            {/* BLE Service Information */}
            <Box sx={{ mb: 2 }}>
              <Typography variant="h6" gutterBottom>
                BLE Service Information
              </Typography>
              <Paper
                variant="outlined"
                sx={{ p: 2, bgcolor: "background.default" }}
              >
                <Grid container spacing={2}>
                  <Grid item xs={12}>
                    <Typography
                      variant="caption"
                      color="text.secondary"
                      display="block"
                    >
                      Service UUID
                    </Typography>
                    <Typography
                      variant="body2"
                      sx={{ fontFamily: "monospace" }}
                    >
                      0000ff00-0000-1000-8000-00805f9b34fb
                    </Typography>
                  </Grid>
                  <Grid item xs={12} sm={6}>
                    <Typography
                      variant="caption"
                      color="text.secondary"
                      display="block"
                    >
                      TX Characteristic
                    </Typography>
                    <Typography
                      variant="body2"
                      sx={{ fontFamily: "monospace" }}
                    >
                      0000ff01-0000-1000-8000-00805f9b34fb
                    </Typography>
                  </Grid>
                  <Grid item xs={12} sm={6}>
                    <Typography
                      variant="caption"
                      color="text.secondary"
                      display="block"
                    >
                      RX Characteristic
                    </Typography>
                    <Typography
                      variant="body2"
                      sx={{ fontFamily: "monospace" }}
                    >
                      0000ff02-0000-1000-8000-00805f9b34fb
                    </Typography>
                  </Grid>
                </Grid>
              </Paper>
            </Box>
          </>
        )}
      </Box>
    </SettingsModal>
  );
}
