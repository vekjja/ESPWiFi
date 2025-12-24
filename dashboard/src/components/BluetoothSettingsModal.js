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
  Tooltip,
} from "@mui/material";
import {
  BluetoothDisabled as BluetoothDisabledIcon,
  Bluetooth as BluetoothIcon,
  BluetoothConnected as BluetoothConnectedIcon,
  GetApp as GetAppIcon,
  ExpandMore as ExpandMoreIcon,
  CheckCircle as CheckCircleIcon,
  Cancel as CancelIcon,
  Storage as StorageIcon,
  CloudDownload as CloudDownloadIcon,
  Info as InfoIcon,
} from "@mui/icons-material";
import SettingsModal from "./SettingsModal";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";

export default function BluetoothSettingsModal({
  open,
  onClose,
  config,
  saveConfig,
  saveConfigToDevice,
  deviceOnline,
  webBleConnected,
  setWebBleConnected,
  webBleDevice,
  setWebBleDevice,
  webBleServer,
  setWebBleServer,
  webBleService,
  setWebBleService,
  webBleTxCharacteristic,
  setWebBleTxCharacteristic,
  webBleRxCharacteristic,
  setWebBleRxCharacteristic,
}) {
  const [loading, setLoading] = useState(false);
  const [enabled, setEnabled] = useState(config?.bluetooth?.enabled || false);
  const initializedRef = useRef(false);

  // Update enabled state when config changes (but only if modal is already initialized)
  useEffect(() => {
    if (initializedRef.current && config?.bluetooth?.enabled !== undefined) {
      setEnabled(config.bluetooth.enabled);
    }
  }, [config?.bluetooth?.enabled]);
  const [fileSystem, setFileSystem] = useState("sd");
  const [files, setFiles] = useState([]);
  const [loadingFiles, setLoadingFiles] = useState(false);
  const [selectedFile, setSelectedFile] = useState("");
  const [fileError, setFileError] = useState(null);
  const [webBleConnecting, setWebBleConnecting] = useState(false);
  const [webBleDownloading, setWebBleDownloading] = useState(false);

  // Use a state variable for connected so we can update it when config changes
  const [connected, setConnected] = useState(
    config?.bluetooth?.connected || false
  );

  // Update connected state when config changes
  useEffect(() => {
    if (config?.bluetooth?.connected !== undefined) {
      setConnected(config.bluetooth.connected);
    }
  }, [config?.bluetooth?.connected]);

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
      // Web Bluetooth state persists in parent component (BluetoothButton)
    }
    // Only reset when modal opens/closes, not when config changes
    // This prevents the enabled state from being reset when user toggles it
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, webBleDevice, webBleTxCharacteristic]);

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

  // Check if Web Bluetooth is available and allowed
  const isWebBluetoothAvailable = () => {
    // Check if the API exists
    if (!navigator.bluetooth) {
      return {
        available: false,
        reason: "Web Bluetooth API is not supported in this browser",
      };
    }

    // Check if we're on HTTPS or localhost (required for Web Bluetooth)
    // Web Bluetooth only works in secure contexts: HTTPS or localhost (even over HTTP)
    const isSecureContext =
      window.isSecureContext ||
      window.location.protocol === "https:" ||
      window.location.hostname === "localhost" ||
      window.location.hostname === "127.0.0.1" ||
      window.location.hostname === "[::1]";

    if (!isSecureContext) {
      return {
        available: false,
        reason:
          "Web Bluetooth requires HTTPS or localhost. The page is currently served over HTTP from the device. To use Web Bluetooth, access the dashboard via localhost (npm run start) or configure HTTPS on the device.",
      };
    }

    return { available: true };
  };

  // Web Bluetooth API - Connect to ESP32 via BLE
  const handleWebBleConnect = async () => {
    const bluetoothCheck = isWebBluetoothAvailable();
    if (!bluetoothCheck.available) {
      setFileError(bluetoothCheck.reason);
      return;
    }

    // Optimistically clear any remote connection state when starting to connect
    // This prevents showing "Remote" during the connection process
    if (connected && !webBleConnected) {
      setConnected(false);
    }

    // Enable Bluetooth first if it's disabled
    if (!enabled) {
      try {
        setEnabled(true);
        setLoading(true);

        const configToSave = {
          ...config,
          bluetooth: {
            ...config.bluetooth,
            enabled: true,
          },
        };

        await saveConfigToDevice(configToSave);
        // Wait a bit for Bluetooth to start
        await new Promise((resolve) => setTimeout(resolve, 1000));
      } catch (err) {
        console.error("Error enabling Bluetooth:", err);
        setFileError(
          "Failed to enable Bluetooth: " + (err.message || "Unknown error")
        );
        setEnabled(false);
        setLoading(false);
        return;
      } finally {
        setLoading(false);
      }
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
    // Optimistically clear connection state immediately to prevent showing "Remote"
    // while waiting for config to update
    setWebBleConnected(false);
    setConnected(false);

    try {
      // Try to stop notifications - may fail if already disconnected
      if (webBleTxCharacteristic) {
        try {
          await webBleTxCharacteristic.stopNotifications();
        } catch (err) {
          // Already disconnected, ignore
          console.log("Notifications already stopped:", err.message);
        }
      }
      // Disconnect if still connected
      if (webBleDevice?.gatt?.connected) {
        webBleDevice.gatt.disconnect();
      }
    } catch (err) {
      // Connection may already be closed, which is fine
      console.log("Web Bluetooth disconnect:", err.message);
    } finally {
      // Always reset state regardless of errors
      setWebBleDevice(null);
      setWebBleServer(null);
      setWebBleService(null);
      setWebBleTxCharacteristic(null);
      setWebBleRxCharacteristic(null);
    }
  };

  // File receiving state (using refs to persist across renders)
  const fileReceiveStateRef = useRef({
    buffer: null,
    fileName: null,
    fileSize: 0,
    received: 0,
    headerProcessed: false,
    lastReceivedTimestamp: null, // Track when we last received data
  });

  // Download file from buffer to device (phone/Mac/browser downloads folder)
  const downloadFileFromBuffer = useCallback((bufferArray, filename) => {
    try {
      console.log("Web Bluetooth - Starting file download from buffer:", {
        filename,
        chunks: bufferArray.length,
      });

      // Combine all chunks into a single ArrayBuffer
      // Handle both ArrayBuffer and ArrayBufferView types
      const totalLength = bufferArray.reduce((sum, chunk) => {
        if (chunk instanceof ArrayBuffer) {
          return sum + chunk.byteLength;
        } else if (chunk.buffer instanceof ArrayBuffer) {
          return sum + chunk.byteLength;
        } else {
          console.warn(
            "Web Bluetooth - Unknown chunk type:",
            typeof chunk,
            chunk
          );
          return sum + (chunk.byteLength || 0);
        }
      }, 0);

      console.log(
        "Web Bluetooth - Combining buffers, total length:",
        totalLength
      );

      const combinedBuffer = new Uint8Array(totalLength);
      let offset = 0;
      for (const chunk of bufferArray) {
        let chunkData;
        if (chunk instanceof ArrayBuffer) {
          chunkData = new Uint8Array(chunk);
        } else if (chunk.buffer instanceof ArrayBuffer) {
          // Handle DataView, TypedArray, etc.
          chunkData = new Uint8Array(
            chunk.buffer,
            chunk.byteOffset || 0,
            chunk.byteLength
          );
        } else {
          console.warn("Web Bluetooth - Skipping invalid chunk:", chunk);
          continue;
        }

        combinedBuffer.set(chunkData, offset);
        offset += chunkData.byteLength;
      }

      console.log("Web Bluetooth - Combined buffer complete:", {
        totalBytes: combinedBuffer.byteLength,
        filename,
      });

      // Create blob
      const blob = new Blob([combinedBuffer], {
        type: "application/octet-stream",
      });

      console.log("Web Bluetooth - Blob created:", {
        size: blob.size,
        type: blob.type,
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

      console.log("Web Bluetooth - Download triggered for:", filename);

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
      if (!value) {
        console.log(
          "Web Bluetooth notification received but value is null/undefined"
        );
        return;
      }

      // Extract ArrayBuffer from DataView
      // value is a DataView, we need to get the underlying buffer
      let buffer;
      let byteLength;
      if (value.buffer) {
        // It's a DataView, extract the ArrayBuffer
        // Note: DataView might have offset/byteLength, so we slice appropriately
        byteLength = value.byteLength;
        if (
          value.byteOffset === 0 &&
          value.byteLength === value.buffer.byteLength
        ) {
          // No offset, can use buffer directly
          buffer = value.buffer;
        } else {
          // Has offset, need to slice
          buffer = value.buffer.slice(
            value.byteOffset,
            value.byteOffset + value.byteLength
          );
        }
      } else if (value instanceof ArrayBuffer) {
        buffer = value;
        byteLength = buffer.byteLength;
      } else {
        console.error(
          "Web Bluetooth - unexpected value type:",
          typeof value,
          value
        );
        return;
      }

      const decoder = new TextDecoder("utf-8");
      const state = fileReceiveStateRef.current;

      if (!state.headerProcessed) {
        // Try to parse file header
        const text = decoder.decode(new Uint8Array(buffer), { stream: true });
        console.log("Web Bluetooth notification - header check:", {
          textLength: text.length,
          firstChars: text.substring(0, 50),
          byteLength: byteLength,
        });

        // Try multiple header formats (header might be split across packets)
        const headerMatch = text.match(/FILE:(.+?):(\d+)\n/);
        if (headerMatch) {
          console.log("Web Bluetooth - header matched:", {
            fileName: headerMatch[1],
            fileSize: headerMatch[2],
          });
          state.fileName = headerMatch[1];
          state.fileSize = parseInt(headerMatch[2], 10);
          state.received = 0;
          state.buffer = [];
          state.headerProcessed = true;
          setWebBleDownloading(true);

          // Extract data after header
          const headerEnd = text.indexOf("\n") + 1;
          if (headerEnd < text.length) {
            // Need to convert back to bytes for the remaining data
            const remainingText = text.substring(headerEnd);
            const encoder = new TextEncoder();
            const dataAfterHeader = encoder.encode(remainingText);
            state.buffer.push(dataAfterHeader.buffer);
            state.received += dataAfterHeader.byteLength;
            console.log(
              "Web Bluetooth - data after header:",
              dataAfterHeader.byteLength,
              "bytes"
            );
          }
        } else {
          // Header might be incomplete, store the partial data
          console.log(
            "Web Bluetooth - header not matched, might be partial or binary data"
          );
          // Store the buffer for later processing
          state.buffer = state.buffer || [];
          state.buffer.push(buffer);
          state.received = (state.received || 0) + byteLength;

          // Try to find header in accumulated buffer
          if (state.received > 20) {
            // We have some data, try to decode accumulated buffer
            const accumulatedText = decoder.decode(
              new Uint8Array(
                state.buffer.reduce((acc, buf) => {
                  const combined = new Uint8Array(
                    acc.byteLength + buf.byteLength
                  );
                  combined.set(new Uint8Array(acc), 0);
                  combined.set(new Uint8Array(buf), acc.byteLength);
                  return combined.buffer;
                }, new ArrayBuffer(0))
              ),
              { stream: true }
            );
            const accumulatedMatch =
              accumulatedText.match(/FILE:(.+?):(\d+)\n/);
            if (accumulatedMatch) {
              console.log("Web Bluetooth - header found in accumulated buffer");
              state.fileName = accumulatedMatch[1];
              state.fileSize = parseInt(accumulatedMatch[2], 10);
              const headerEnd = accumulatedText.indexOf("\n") + 1;
              // Reset buffer with data after header
              const encoder = new TextEncoder();
              const dataAfterHeader = encoder.encode(
                accumulatedText.substring(headerEnd)
              );
              state.buffer = [dataAfterHeader.buffer];
              state.received = dataAfterHeader.byteLength;
              state.headerProcessed = true;
              setWebBleDownloading(true);
            }
          }
        }
      } else {
        // Continuation of file data
        state.buffer.push(buffer);
        state.received += byteLength;
        state.lastReceivedTimestamp = Date.now(); // Update timestamp

        console.log("Web Bluetooth - data chunk received:", {
          chunkSize: byteLength,
          totalReceived: state.received,
          fileSize: state.fileSize,
          remaining: state.fileSize - state.received,
          progress: ((state.received / state.fileSize) * 100).toFixed(1) + "%",
        });

        // Check if we've received the complete file (allow small rounding errors)
        // Use a threshold since we might receive slightly more or less due to header handling
        const threshold = 10; // Allow 10 bytes difference
        if (state.received >= state.fileSize - threshold) {
          console.log("Web Bluetooth - file complete, triggering download:", {
            fileName: state.fileName,
            fileSize: state.fileSize,
            received: state.received,
            difference: state.received - state.fileSize,
            chunks: state.buffer.length,
          });
          // File complete - trigger download
          downloadFileFromBuffer(state.buffer, state.fileName);

          // Reset state
          state.buffer = [];
          state.fileName = null;
          state.fileSize = 0;
          state.received = 0;
          state.headerProcessed = false;
          state.lastReceivedTimestamp = null;
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

    if (!webBleTxCharacteristic) {
      setFileError("Web Bluetooth characteristic not available");
      return;
    }

    setWebBleDownloading(true);
    setFileError(null);

    // Reset file receive state
    fileReceiveStateRef.current = {
      buffer: [],
      fileName: null,
      fileSize: 0,
      received: 0,
      headerProcessed: false,
    };

    console.log("Web Bluetooth - Starting file download:", {
      file: file.display,
      path: file.path,
      fs: file.fs,
      characteristic: !!webBleTxCharacteristic,
      deviceConnected: webBleDevice?.gatt?.connected,
    });

    try {
      // Request file from ESP32 via HTTP (trigger send)
      // This will cause the ESP32 to send the file via BLE
      const url = buildApiUrl(
        `/api/bluetooth/send?fs=${file.fs}&path=${encodeURIComponent(
          file.path
        )}`
      );
      console.log("Web Bluetooth - Requesting file send via HTTP:", url);

      const response = await fetch(url, getFetchOptions({ method: "POST" }));

      if (!response.ok) {
        const errorData = await response.json().catch(() => ({}));
        throw new Error(errorData.error || "Failed to trigger file send");
      }

      const result = await response.json();
      console.log(
        "Web Bluetooth - File send triggered, waiting for BLE notifications:",
        result
      );

      // File will be received via BLE notifications and handled by handleWebBleNotification
      // Set up periodic checking to detect stalled downloads
      const startTime = Date.now();
      const downloadCheckInterval = setInterval(() => {
        const state = fileReceiveStateRef.current;
        const now = Date.now();

        if (state.headerProcessed) {
          // Check if file is complete (with small threshold for rounding)
          const threshold = 10;
          if (state.received >= state.fileSize - threshold) {
            // File should be complete - clear interval (download will trigger in handler)
            clearInterval(downloadCheckInterval);
            return;
          }

          // Check if download is stalled (no new data for 5 seconds)
          if (state.lastReceivedTimestamp) {
            const timeSinceLastData = now - state.lastReceivedTimestamp;
            if (timeSinceLastData > 5000) {
              // No new data for 5 seconds - consider stalled
              const progress = (
                (state.received / state.fileSize) *
                100
              ).toFixed(1);
              console.warn("Web Bluetooth - Download stalled, no new data:", {
                received: state.received,
                expected: state.fileSize,
                progress: progress + "%",
                timeSinceLastData: timeSinceLastData,
              });
              clearInterval(downloadCheckInterval);
              setFileError(
                `Download stalled at ${progress}%. No new data received for ${(
                  timeSinceLastData / 1000
                ).toFixed(1)} seconds. Received ${state.received} of ${
                  state.fileSize
                } bytes.`
              );
              setWebBleDownloading(false);
              return;
            }
          }
        } else {
          // No header received yet
          const timeSinceStart = now - startTime;
          if (timeSinceStart > 10000) {
            // 10 seconds with no header
            console.error("Web Bluetooth - No file header received");
            clearInterval(downloadCheckInterval);
            setFileError(
              "No file data received via Web Bluetooth. Please check connection."
            );
            setWebBleDownloading(false);
          }
        }

        // Overall timeout check (absolute maximum time)
        const totalTime = now - startTime;
        const estimatedTimeout = Math.max(120000, (result.fileSize || 0) / 50); // At least 2 minutes, or ~50 bytes/second
        if (totalTime > estimatedTimeout) {
          console.warn("Web Bluetooth - Download timeout:", {
            received: state.received,
            expected: state.fileSize,
            totalTime: (totalTime / 1000).toFixed(1) + "s",
          });
          clearInterval(downloadCheckInterval);
          if (state.headerProcessed && state.received < state.fileSize - 10) {
            const progress = ((state.received / state.fileSize) * 100).toFixed(
              1
            );
            setFileError(
              `Download timed out at ${progress}% after ${(
                totalTime / 1000
              ).toFixed(1)} seconds. Received ${state.received} of ${
                state.fileSize
              } bytes.`
            );
          } else {
            setFileError("Download timeout - no file data received.");
          }
          setWebBleDownloading(false);
        }
      }, 1000); // Check every second
    } catch (err) {
      console.error("Error downloading file:", err);
      setFileError(err.message || "Failed to download file");
      setWebBleDownloading(false);
    }
  };

  // Re-subscribe to notifications when webBleTxCharacteristic is restored (e.g., modal reopens)
  useEffect(() => {
    if (webBleTxCharacteristic && webBleDevice?.gatt?.connected) {
      webBleTxCharacteristic
        .startNotifications()
        .then(() => {
          if (webBleTxCharacteristic) {
            webBleTxCharacteristic.addEventListener(
              "characteristicvaluechanged",
              handleWebBleNotification
            );
          }
        })
        .catch(() => {
          // Already subscribed or failed, that's okay
        });
    }

    return () => {
      // Cleanup: remove event listener when characteristic changes or component unmounts
      if (webBleTxCharacteristic) {
        // Note: We don't stop notifications here to allow connection to persist
        // The event listener will be cleaned up automatically
      }
    };
  }, [webBleTxCharacteristic, webBleDevice, handleWebBleNotification]);

  // Handle device disconnection event (but don't disconnect on modal close)
  useEffect(() => {
    if (!webBleDevice) return;

    const handleDisconnect = () => {
      // Only handle disconnection if it happens naturally (not on modal close)
      handleWebBleDisconnect();
    };

    webBleDevice.addEventListener("gattserverdisconnected", handleDisconnect);

    return () => {
      // Only clean up event listener, don't disconnect the device
      // This allows the connection to persist when the modal is closed
      webBleDevice.removeEventListener(
        "gattserverdisconnected",
        handleDisconnect
      );
    };
  }, [webBleDevice, handleWebBleDisconnect]);

  const deviceName = config?.deviceName || "";
  const address = config?.bluetooth?.address || "";

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <Tooltip
          title={
            enabled ? "Click to disable Bluetooth" : "Click to enable Bluetooth"
          }
        >
          <Box
            onClick={handleToggleEnabled}
            sx={{
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              gap: 1,
              cursor: "pointer",
              "&:hover": {
                opacity: 0.8,
              },
            }}
          >
            {(() => {
              if (webBleConnected) {
                return (
                  <BluetoothConnectedIcon sx={{ color: "primary.main" }} />
                );
              } else if (connected) {
                return (
                  <BluetoothConnectedIcon sx={{ color: "warning.main" }} />
                );
              } else if (enabled) {
                return <BluetoothIcon sx={{ color: "primary.main" }} />;
              } else {
                return (
                  <BluetoothDisabledIcon sx={{ color: "text.disabled" }} />
                );
              }
            })()}
            Bluetooth
          </Box>
        </Tooltip>
      }
      maxWidth="md"
    >
      <Box>
        <Stack spacing={3}>
          {/* Bluetooth Info Card */}
          <Card
            elevation={2}
            sx={{
              bgcolor: "background.paper",
            }}
          >
            <CardContent>
              {fileError && (
                <Alert severity="error" sx={{ mb: 2 }}>
                  {fileError}
                </Alert>
              )}
              {(() => {
                const bluetoothCheck = isWebBluetoothAvailable();
                if (!bluetoothCheck.available && !fileError) {
                  return (
                    <Alert severity="warning" sx={{ mb: 2 }}>
                      {bluetoothCheck.reason}
                    </Alert>
                  );
                }
                return null;
              })()}
              {/* Status Information - Better aligned */}
              <Grid container spacing={3} sx={{ mb: 3 }}>
                <Grid item xs={12} sm={6}>
                  <Box
                    sx={{
                      display: "flex",
                      flexDirection: "column",
                      alignItems: "center",
                      justifyContent: "center",
                      gap: 2.5,
                      width: "100%",
                      minHeight: "100px",
                    }}
                  >
                    {(() => {
                      let connectionLabel = "No Connections";
                      let connectionColor = "default";
                      let connectionIcon = <CancelIcon />;
                      let connectionTooltip =
                        "Click to connect via Web Bluetooth";
                      let isClickable = false;

                      if (webBleConnected) {
                        connectionLabel = "Connected via Web Bluetooth";
                        connectionColor = "success";
                        connectionIcon = <CheckCircleIcon />;
                        connectionTooltip = "Click to disconnect Web Bluetooth";
                        isClickable = true;
                      } else if (connected && !webBleConnecting) {
                        // Only show "Remote" if we're not currently connecting via Web Bluetooth
                        // This prevents the jarring transition from "Remote" to "Connected"
                        connectionLabel = "Remote Device Connected";
                        connectionColor = "warning";
                        connectionIcon = <InfoIcon />;
                        connectionTooltip =
                          "Remote connection active (another device is connected)";
                        isClickable = false;
                      } else {
                        // Not Connected - make it clickable
                        isClickable = true;
                      }

                      const canClick =
                        isClickable && !webBleConnecting && !loading;
                      const handleClick = () => {
                        if (webBleConnected) {
                          handleWebBleDisconnect();
                        } else if (isClickable) {
                          handleWebBleConnect();
                        }
                      };
                      return (
                        <Tooltip title={connectionTooltip}>
                          <Chip
                            icon={connectionIcon}
                            label={connectionLabel}
                            color={connectionColor}
                            onClick={canClick ? handleClick : undefined}
                            sx={
                              canClick
                                ? {
                                    cursor: "pointer",
                                    "&:hover": {
                                      opacity: 0.8,
                                    },
                                  }
                                : undefined
                            }
                          />
                        </Tooltip>
                      );
                    })()}
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
              </Grid>
              {/* File Download - Only show when Web Bluetooth is connected */}
              {webBleConnected && (
                <Box sx={{ mt: 3 }}>
                  <Box
                    sx={{
                      display: "flex",
                      alignItems: "center",
                      justifyContent: "center",
                      mb: 2,
                    }}
                  >
                    <CloudDownloadIcon
                      sx={{ mr: 1.5, fontSize: 24, color: "primary.main" }}
                    />
                    <Typography variant="h6">
                      Download File to Device
                    </Typography>
                  </Box>

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
                    disabled={
                      !selectedFile || webBleDownloading || loadingFiles
                    }
                    fullWidth
                    sx={{
                      py: 1.5,
                      fontSize: "1rem",
                      fontWeight: 600,
                    }}
                  >
                    {webBleDownloading
                      ? "Downloading..."
                      : "Download to Device"}
                  </Button>
                </Box>
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
