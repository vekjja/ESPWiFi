import React, { useState, useEffect, useRef, useCallback } from "react";
import {
  Box,
  Typography,
  Button,
  Paper,
  Grid,
  MenuItem,
  Select,
  FormControl,
  InputLabel,
  ToggleButtonGroup,
  ToggleButton,
  Alert,
  CircularProgress,
  Card,
  CardContent,
  Chip,
  Stack,
  Accordion,
  AccordionSummary,
  AccordionDetails,
} from "@mui/material";
import {
  BluetoothDisabled as BluetoothDisabledIcon,
  Bluetooth as BluetoothIcon,
  GetApp as GetAppIcon,
  ExpandMore as ExpandMoreIcon,
  CheckCircle as CheckCircleIcon,
  Cancel as CancelIcon,
  Storage as StorageIcon,
  CloudDownload as CloudDownloadIcon,
  Info as InfoIcon,
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
  const [fileError, setFileError] = useState(null);
  const [webBleConnected, setWebBleConnected] = useState(false);
  const [webBleDevice, setWebBleDevice] = useState(null);
  // eslint-disable-next-line no-unused-vars
  const [webBleServer, setWebBleServer] = useState(null); // Reserved for future use (sending files)
  // eslint-disable-next-line no-unused-vars
  const [webBleService, setWebBleService] = useState(null); // Reserved for future use
  const [webBleTxCharacteristic, setWebBleTxCharacteristic] = useState(null);
  // eslint-disable-next-line no-unused-vars
  const [webBleRxCharacteristic, setWebBleRxCharacteristic] = useState(null); // Reserved for future use (sending files)
  const [webBleConnecting, setWebBleConnecting] = useState(false);
  const [webBleDownloading, setWebBleDownloading] = useState(false);

  const connected = config?.bluetooth?.connected || false;

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

  // Load files when modal opens and Bluetooth is enabled and connected
  useEffect(() => {
    if (open && enabled && connected && deviceOnline) {
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
    } else if (!connected) {
      // Clear files when disconnected
      setFiles([]);
      setSelectedFile("");
    }
  }, [open, enabled, connected, deviceOnline, fileSystem, fetchAllFiles]);

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

  const handleToggleEnabled = async () => {
    if (loading || !deviceOnline || !config || !saveConfigToDevice) {
      return;
    }

    const newEnabled = !enabled;
    const previousEnabled = enabled;
    setEnabled(newEnabled);
    setLoading(true);

    try {
      const configToSave = {
        ...config,
        bluetooth: {
          ...config.bluetooth,
          enabled: newEnabled,
        },
      };

      // Save to device immediately
      saveConfigToDevice(configToSave);
    } catch (err) {
      console.error("Error toggling Bluetooth:", err);
      // Revert on error
      setEnabled(previousEnabled);
    } finally {
      setLoading(false);
    }
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

  // Web Bluetooth API - Connect to ESP32 via BLE
  const handleWebBleConnect = async () => {
    if (!navigator.bluetooth) {
      setFileError("Web Bluetooth API is not supported in this browser");
      return;
    }

    setWebBleConnecting(true);
    setFileError(null);

    try {
      // Service and characteristic UUIDs
      const serviceUuid = "0000ff00-0000-1000-8000-00805f9b34fb";
      const txCharacteristicUuid = "0000ff01-0000-1000-8000-00805f9b34fb";
      const rxCharacteristicUuid = "0000ff02-0000-1000-8000-00805f9b34fb";

      // Request device with service UUID
      const device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [serviceUuid] }],
        optionalServices: [serviceUuid],
      });

      // Connect to GATT server
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(serviceUuid);

      // Get characteristics
      const txCharacteristic = await service.getCharacteristic(
        txCharacteristicUuid
      );
      const rxCharacteristic = await service.getCharacteristic(
        rxCharacteristicUuid
      );

      // Subscribe to notifications from TX characteristic (device sends data here)
      await txCharacteristic.startNotifications();
      txCharacteristic.addEventListener(
        "characteristicvaluechanged",
        handleWebBleNotification
      );

      // Reset file receive state
      fileReceiveStateRef.current = {
        buffer: null,
        fileName: null,
        fileSize: 0,
        received: 0,
        headerProcessed: false,
      };

      setWebBleDevice(device);
      setWebBleServer(server);
      setWebBleService(service);
      setWebBleTxCharacteristic(txCharacteristic);
      setWebBleRxCharacteristic(rxCharacteristic);
      setWebBleConnected(true);
    } catch (err) {
      if (err.name !== "NotFoundError") {
        setFileError(err.message || "Failed to connect via Web Bluetooth");
        console.error("Error connecting via Web Bluetooth:", err);
      }
    } finally {
      setWebBleConnecting(false);
    }
  };

  // Handle Web Bluetooth disconnection
  const handleWebBleDisconnect = async () => {
    try {
      if (webBleTxCharacteristic) {
        await webBleTxCharacteristic.stopNotifications();
      }
      if (webBleDevice?.gatt?.connected) {
        webBleDevice.gatt.disconnect();
      }
    } catch (err) {
      console.error("Error disconnecting Web Bluetooth:", err);
    } finally {
      setWebBleDevice(null);
      setWebBleServer(null);
      setWebBleService(null);
      setWebBleTxCharacteristic(null);
      setWebBleRxCharacteristic(null);
      setWebBleConnected(false);
    }
  };

  // File receiving state (using refs to persist across renders)
  const fileReceiveStateRef = useRef({
    buffer: null,
    fileName: null,
    fileSize: 0,
    received: 0,
    headerProcessed: false,
  });

  // Download file from buffer to device (phone/Mac/browser downloads folder)
  const downloadFileFromBuffer = useCallback((bufferArray, filename) => {
    try {
      // Combine all chunks into a single ArrayBuffer
      const totalLength = bufferArray.reduce(
        (sum, chunk) => sum + chunk.byteLength,
        0
      );
      const combinedBuffer = new Uint8Array(totalLength);
      let offset = 0;
      for (const chunk of bufferArray) {
        combinedBuffer.set(new Uint8Array(chunk.buffer), offset);
        offset += chunk.byteLength;
      }

      // Create blob
      const blob = new Blob([combinedBuffer], {
        type: "application/octet-stream",
      });

      // Create download link
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = filename;
      a.style.display = "none"; // Hide the link

      // Append to body, click, then remove
      document.body.appendChild(a);
      a.click();

      // Clean up after a short delay to ensure download starts
      setTimeout(() => {
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
      }, 100);

      // Show success message
      setFileError(null);
    } catch (err) {
      console.error("Error downloading file:", err);
      setFileError("Failed to download file: " + err.message);
      setWebBleDownloading(false);
    }
  }, []);

  // Handle notifications from ESP32 (file data)
  const handleWebBleNotification = useCallback(
    (event) => {
      const value = event.target.value;
      const decoder = new TextDecoder("utf-8");
      const state = fileReceiveStateRef.current;

      if (!state.headerProcessed) {
        // Try to parse file header
        const text = decoder.decode(value);
        const headerMatch = text.match(/^FILE:(.+?):(\d+)\n/);
        if (headerMatch) {
          state.fileName = headerMatch[1];
          state.fileSize = parseInt(headerMatch[2], 10);
          state.received = 0;
          state.buffer = [];
          state.headerProcessed = true;
          setWebBleDownloading(true);

          // Extract data after header
          const headerEnd = text.indexOf("\n") + 1;
          if (headerEnd < value.byteLength) {
            const dataAfterHeader = value.slice(headerEnd);
            state.buffer.push(dataAfterHeader);
            state.received += dataAfterHeader.byteLength;
          }
        }
      } else {
        // Continuation of file data
        state.buffer.push(value);
        state.received += value.byteLength;

        // Check if we've received the complete file
        if (state.received >= state.fileSize) {
          // File complete - trigger download
          downloadFileFromBuffer(state.buffer, state.fileName);

          // Reset state
          state.buffer = null;
          state.fileName = null;
          state.fileSize = 0;
          state.received = 0;
          state.headerProcessed = false;
          setWebBleDownloading(false);
        }
      }
    },
    [downloadFileFromBuffer]
  );

  // Download file from Web Bluetooth to device (phone/Mac)
  const handleWebBleDownloadFile = async () => {
    if (!webBleConnected || !selectedFile) {
      setFileError("Please connect via Web Bluetooth and select a file");
      return;
    }

    const file = files.find((f) => f.display === selectedFile);
    if (!file) {
      setFileError("Selected file not found");
      return;
    }

    setWebBleDownloading(true);
    setFileError(null);

    // Reset file receive state
    fileReceiveStateRef.current = {
      buffer: null,
      fileName: null,
      fileSize: 0,
      received: 0,
      headerProcessed: false,
    };

    try {
      // Request file from ESP32 via HTTP (trigger send)
      // This will cause the ESP32 to send the file via BLE
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
        throw new Error(errorData.error || "Failed to trigger file send");
      }

      await response.json();
      // File will be received via BLE notifications and handled by handleWebBleNotification
      // The download will be triggered automatically when the file is complete
    } catch (err) {
      setFileError(err.message || "Failed to download file");
      console.error("Error downloading file:", err);
      setWebBleDownloading(false);
    }
  };

  // Handle device disconnection and cleanup
  useEffect(() => {
    if (!webBleDevice) return;

    const handleDisconnect = () => {
      handleWebBleDisconnect();
    };

    webBleDevice.addEventListener("gattserverdisconnected", handleDisconnect);

    return () => {
      webBleDevice.removeEventListener(
        "gattserverdisconnected",
        handleDisconnect
      );
      if (webBleDevice?.gatt?.connected) {
        handleWebBleDisconnect();
      }
    };
  }, [webBleDevice]);

  const deviceName = config?.deviceName || "";
  const address = config?.bluetooth?.address || "";

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
      <Box>
        <Stack spacing={3}>
          {/* File Download - Only show when Web Bluetooth is connected */}
          {webBleConnected && (
            <Card
              elevation={3}
              sx={{
                bgcolor: "background.paper",
                border: 2,
                borderColor: "primary.main",
              }}
            >
              <CardContent>
                <Box sx={{ display: "flex", alignItems: "center", mb: 2 }}>
                  <CloudDownloadIcon
                    sx={{ mr: 1.5, fontSize: 28, color: "primary.main" }}
                  />
                  <Box sx={{ flex: 1 }}>
                    <Typography variant="h6" gutterBottom>
                      Download File to Device
                    </Typography>
                    <Typography variant="body2" color="text.secondary">
                      Select a file from the ESP32 to download to your phone or
                      Mac
                    </Typography>
                  </Box>
                </Box>

                {fileError && (
                  <Alert severity="error" sx={{ mb: 2 }}>
                    {fileError}
                  </Alert>
                )}

                {/* Filesystem Selector */}
                <Box sx={{ mb: 2 }}>
                  <Typography variant="subtitle2" sx={{ mb: 1 }}>
                    <StorageIcon
                      sx={{ fontSize: 16, mr: 0.5, verticalAlign: "middle" }}
                    />
                    Storage Location
                  </Typography>
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
                    fullWidth
                    sx={{
                      "& .MuiToggleButton-root": {
                        py: 1.5,
                        fontSize: "0.875rem",
                        fontWeight: 500,
                      },
                    }}
                  >
                    <ToggleButton value="sd" aria-label="SD card">
                      SD Card
                    </ToggleButton>
                    <ToggleButton value="lfs" aria-label="LittleFS">
                      LittleFS
                    </ToggleButton>
                  </ToggleButtonGroup>
                </Box>

                {/* File Selector */}
                <FormControl fullWidth sx={{ mb: 3 }}>
                  <InputLabel>Select File</InputLabel>
                  <Select
                    value={selectedFile}
                    onChange={(e) => setSelectedFile(e.target.value)}
                    label="Select File"
                    disabled={loadingFiles || webBleDownloading}
                    sx={{
                      "& .MuiSelect-select": {
                        py: 1.5,
                      },
                    }}
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
                          <Box
                            sx={{
                              display: "flex",
                              justifyContent: "space-between",
                              width: "100%",
                            }}
                          >
                            <Typography>{file.display}</Typography>
                            <Typography
                              variant="caption"
                              color="text.secondary"
                              sx={{ ml: 2 }}
                            >
                              {(file.size / 1024).toFixed(2)} KB
                            </Typography>
                          </Box>
                        </MenuItem>
                      ))
                    )}
                  </Select>
                </FormControl>

                {/* Download Button */}
                <Button
                  variant="contained"
                  color="primary"
                  size="large"
                  startIcon={
                    webBleDownloading ? (
                      <CircularProgress size={20} color="inherit" />
                    ) : (
                      <GetAppIcon />
                    )
                  }
                  onClick={handleWebBleDownloadFile}
                  disabled={!selectedFile || webBleDownloading || loadingFiles}
                  fullWidth
                  sx={{
                    py: 1.5,
                    fontSize: "1rem",
                    fontWeight: 600,
                  }}
                >
                  {webBleDownloading ? "Downloading..." : "Download to Device"}
                </Button>
              </CardContent>
            </Card>
          )}

          {/* Bluetooth Info Card */}
          <Card
            elevation={2}
            sx={{
              bgcolor: "background.paper",
            }}
          >
            <CardContent>
              {/* Status Information - Better aligned */}
              <Grid container spacing={3} sx={{ mb: 3 }}>
                <Grid item xs={12} sm={6}>
                  <Box sx={{ display: "flex", alignItems: "center", mb: 2 }}>
                    <Typography
                      variant="subtitle2"
                      color="text.secondary"
                      sx={{ mr: 1.5, minWidth: 140 }}
                    >
                      Status:
                    </Typography>
                    <Box sx={{ minWidth: 140 }}>
                      <Chip
                        icon={enabled ? <CheckCircleIcon /> : <CancelIcon />}
                        label={enabled ? "Enabled" : "Disabled"}
                        color={enabled ? "success" : "default"}
                        size="small"
                        onClick={handleToggleEnabled}
                        sx={{
                          cursor: "pointer",
                          "&:hover": {
                            opacity: 0.8,
                          },
                        }}
                      />
                    </Box>
                  </Box>
                  <Box sx={{ display: "flex", alignItems: "center", mb: 2 }}>
                    <Typography
                      variant="subtitle2"
                      color="text.secondary"
                      sx={{ mr: 1.5, minWidth: 140 }}
                    >
                      Connection:
                    </Typography>
                    <Box sx={{ minWidth: 140 }}>
                      <Chip
                        icon={connected ? <CheckCircleIcon /> : <CancelIcon />}
                        label={connected ? "Connected" : "Not Connected"}
                        color={connected ? "success" : "default"}
                        size="small"
                      />
                    </Box>
                  </Box>
                </Grid>
                <Grid item xs={12} sm={6}>
                  {deviceName && (
                    <Box sx={{ mb: 2 }}>
                      <Typography
                        variant="subtitle2"
                        color="text.secondary"
                        gutterBottom
                      >
                        Device Name
                      </Typography>
                      <Typography variant="body2" sx={{ fontWeight: 500 }}>
                        {deviceName}
                      </Typography>
                    </Box>
                  )}
                  {address && (
                    <Box>
                      <Typography
                        variant="subtitle2"
                        color="text.secondary"
                        gutterBottom
                      >
                        MAC Address
                      </Typography>
                      <Typography
                        variant="body2"
                        sx={{ fontFamily: "monospace", fontWeight: 500 }}
                      >
                        {address}
                      </Typography>
                    </Box>
                  )}
                </Grid>
                {webBleConnected && (
                  <Grid item xs={12}>
                    <Box sx={{ display: "flex", alignItems: "center" }}>
                      <Typography
                        variant="subtitle2"
                        color="text.secondary"
                        sx={{ mr: 1.5, minWidth: 120 }}
                      >
                        Web Bluetooth:
                      </Typography>
                      <Chip
                        icon={<CheckCircleIcon />}
                        label="Connected"
                        color="success"
                        size="small"
                      />
                    </Box>
                  </Grid>
                )}
              </Grid>
              {webBleConnected ? (
                <Button
                  variant="contained"
                  color="error"
                  startIcon={<BluetoothDisabledIcon />}
                  onClick={handleWebBleDisconnect}
                  disabled={webBleConnecting}
                  fullWidth
                  size="large"
                  sx={{ mb: connected ? 2 : 0 }}
                >
                  Disconnect Web Bluetooth
                </Button>
              ) : (
                <Button
                  variant="contained"
                  color="primary"
                  startIcon={
                    webBleConnecting ? (
                      <CircularProgress size={20} color="inherit" />
                    ) : (
                      <BluetoothIcon />
                    )
                  }
                  onClick={handleWebBleConnect}
                  disabled={webBleConnecting || !enabled}
                  fullWidth
                  size="large"
                  sx={{ mb: connected ? 2 : 0 }}
                >
                  {webBleConnecting
                    ? "Connecting..."
                    : "Connect via Web Bluetooth"}
                </Button>
              )}
              {connected && (
                <Button
                  variant="outlined"
                  color="error"
                  startIcon={<BluetoothDisabledIcon />}
                  onClick={handleDisconnect}
                  disabled={loading}
                  fullWidth
                >
                  Disconnect All Devices
                </Button>
              )}
            </CardContent>
          </Card>

          {/* BLE Service Information - Collapsible */}
          <Accordion
            elevation={2}
            sx={{
              bgcolor: "background.paper",
              "&:before": { display: "none" },
            }}
          >
            <AccordionSummary
              expandIcon={<ExpandMoreIcon />}
              sx={{
                bgcolor: "background.default",
                "&:hover": { bgcolor: "action.hover" },
              }}
            >
              <Box sx={{ display: "flex", alignItems: "center" }}>
                <InfoIcon sx={{ mr: 1, color: "text.secondary" }} />
                <Typography variant="subtitle1">
                  BLE Service Information
                </Typography>
              </Box>
            </AccordionSummary>
            <AccordionDetails>
              <Grid container spacing={2}>
                <Grid item xs={12}>
                  <Typography
                    variant="caption"
                    color="text.secondary"
                    display="block"
                    gutterBottom
                  >
                    Service UUID
                  </Typography>
                  <Paper
                    variant="outlined"
                    sx={{
                      p: 1.5,
                      bgcolor: "background.default",
                      fontFamily: "monospace",
                      fontSize: "0.875rem",
                    }}
                  >
                    0000ff00-0000-1000-8000-00805f9b34fb
                  </Paper>
                </Grid>
                <Grid item xs={12} sm={6}>
                  <Typography
                    variant="caption"
                    color="text.secondary"
                    display="block"
                    gutterBottom
                  >
                    TX Characteristic
                  </Typography>
                  <Paper
                    variant="outlined"
                    sx={{
                      p: 1.5,
                      bgcolor: "background.default",
                      fontFamily: "monospace",
                      fontSize: "0.875rem",
                    }}
                  >
                    0000ff01-0000-1000-8000-00805f9b34fb
                  </Paper>
                </Grid>
                <Grid item xs={12} sm={6}>
                  <Typography
                    variant="caption"
                    color="text.secondary"
                    display="block"
                    gutterBottom
                  >
                    RX Characteristic
                  </Typography>
                  <Paper
                    variant="outlined"
                    sx={{
                      p: 1.5,
                      bgcolor: "background.default",
                      fontFamily: "monospace",
                      fontSize: "0.875rem",
                    }}
                  >
                    0000ff02-0000-1000-8000-00805f9b34fb
                  </Paper>
                </Grid>
              </Grid>
            </AccordionDetails>
          </Accordion>
        </Stack>
      </Box>
    </SettingsModal>
  );
}
