import React, { useState } from "react";
import {
  Box,
  Typography,
  Button,
  ListItemText,
  ListItemButton,
  CircularProgress,
  Chip,
  Divider,
  Alert,
  Paper,
  Collapse,
  TextField,
} from "@mui/material";
import BluetoothIcon from "@mui/icons-material/Bluetooth";
import ExpandMoreIcon from "@mui/icons-material/ExpandMore";
import ExpandLessIcon from "@mui/icons-material/ExpandLess";
import ReadIcon from "@mui/icons-material/Visibility";
import WriteIcon from "@mui/icons-material/Edit";
import SettingsModal from "./SettingsModal";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";

export default function BluetoothSettingsModal({
  open,
  onClose,
  config,
  saveConfig,
  saveConfigToDevice,
  deviceOnline,
  bleStatus,
}) {
  const [connectedDevice, setConnectedDevice] = useState(null);
  const [services, setServices] = useState([]);
  const [expandedService, setExpandedService] = useState(null);
  const [error, setError] = useState(null);
  const [writeValue, setWriteValue] = useState("");
  const [loading, setLoading] = useState(false);
  const [characteristicValues, setCharacteristicValues] = useState({});

  // Check if BLE is actually running (not just configured)
  const isBLERunning = bleStatus?.status !== "not_initialized";

  // Check if Web Bluetooth is supported
  const isWebBluetoothSupported = () => {
    return navigator.bluetooth !== undefined;
  };

  const handleToggleESPBLE = async () => {
    if (!deviceOnline) return;

    setLoading(true);
    try {
      // Use actual runtime status, not config
      const endpoint = isBLERunning ? "/api/ble/stop" : "/api/ble/start";
      const url = buildApiUrl(endpoint);
      const options = getFetchOptions({ method: "POST" });
      const response = await fetch(url, options);

      if (response.ok) {
        const data = await response.json();
        console.log("BLE toggle response:", data);

        // Update config to match the action
        const newEnabled = !isBLERunning;
        const updated = {
          ...(config || {}),
          ble: {
            ...(config?.ble || {}),
            enabled: newEnabled,
          },
        };
        saveConfig?.(updated);
        if (saveConfigToDevice) {
          await Promise.resolve(saveConfigToDevice(updated));
        }
        setError(null);
      } else {
        const data = await response.json();
        setError(data.message || "Failed to toggle BLE");
      }
    } catch (err) {
      setError("Network error: " + err.message);
    } finally {
      setLoading(false);
    }
  };

  const handleScan = async () => {
    if (!isWebBluetoothSupported()) {
      setError(
        "Web Bluetooth is not supported in this browser. Please use Chrome, Edge, or Opera."
      );
      return;
    }

    setLoading(true);
    setError(null);

    try {
      // Request a BLE device
      const device = await navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
        optionalServices: [
          "generic_access",
          "device_information",
          "battery_service",
        ],
      });

      console.log("Selected device:", device.name);
      await connectToDevice(device);
    } catch (err) {
      if (err.name === "NotFoundError") {
        setError("No device selected");
      } else {
        setError("Scan failed: " + err.message);
      }
      setLoading(false);
    }
  };

  const connectToDevice = async (device) => {
    try {
      setLoading(true);
      setError(null);

      // Connect to GATT server
      console.log("Connecting to GATT Server...");
      const server = await device.gatt.connect();

      console.log("Getting Primary Services...");
      const primaryServices = await server.getPrimaryServices();

      console.log(`Found ${primaryServices.length} services`);

      // Build services array with characteristics
      const servicesData = [];
      for (const service of primaryServices) {
        const characteristics = await service.getCharacteristics();

        const charsData = characteristics.map((char) => {
          const props = [];
          if (char.properties.read) props.push("read");
          if (char.properties.write) props.push("write");
          if (char.properties.writeWithoutResponse)
            props.push("write-without-response");
          if (char.properties.notify) props.push("notify");
          if (char.properties.indicate) props.push("indicate");

          return {
            uuid: char.uuid,
            name: getCharacteristicName(char.uuid),
            properties: props,
            characteristic: char, // Store actual characteristic object
          };
        });

        servicesData.push({
          uuid: service.uuid,
          name: getServiceName(service.uuid),
          characteristics: charsData,
          service: service, // Store actual service object
        });
      }

      setConnectedDevice({
        name: device.name || "Unknown Device",
        id: device.id,
        device: device,
        server: server,
      });
      setServices(servicesData);
      setLoading(false);
    } catch (err) {
      setError("Connection failed: " + err.message);
      setLoading(false);
    }
  };

  const handleDisconnect = () => {
    if (connectedDevice?.device?.gatt?.connected) {
      connectedDevice.device.gatt.disconnect();
    }
    setConnectedDevice(null);
    setServices([]);
    setExpandedService(null);
    setCharacteristicValues({});
  };

  const handleRead = async (serviceUuid, charUuid, characteristic) => {
    try {
      setLoading(true);
      const value = await characteristic.readValue();

      // Convert DataView to hex string
      const hexString = Array.from(new Uint8Array(value.buffer))
        .map((b) => b.toString(16).padStart(2, "0"))
        .join(" ");

      // Try to decode as UTF-8 text
      const decoder = new TextDecoder("utf-8");
      let textValue = "";
      try {
        textValue = decoder.decode(value);
      } catch (e) {
        textValue = "(binary data)";
      }

      const key = `${serviceUuid}-${charUuid}`;
      setCharacteristicValues((prev) => ({
        ...prev,
        [key]: {
          hex: hexString,
          text: textValue,
          timestamp: new Date().toLocaleTimeString(),
        },
      }));

      setLoading(false);
    } catch (err) {
      setError("Read failed: " + err.message);
      setLoading(false);
    }
  };

  const handleWrite = async (serviceUuid, charUuid, characteristic, value) => {
    try {
      setLoading(true);

      // Check if value is hex format (space-separated hex bytes)
      let dataToWrite;
      if (/^[0-9a-fA-F\s]+$/.test(value)) {
        // Hex format
        const hexBytes = value.split(/\s+/).filter((b) => b.length > 0);
        const bytes = hexBytes.map((hex) => parseInt(hex, 16));
        dataToWrite = new Uint8Array(bytes);
      } else {
        // Text format - encode as UTF-8
        const encoder = new TextEncoder();
        dataToWrite = encoder.encode(value);
      }

      await characteristic.writeValue(dataToWrite);

      setWriteValue("");
      setError(null);

      // Show success briefly
      const key = `${serviceUuid}-${charUuid}`;
      setCharacteristicValues((prev) => ({
        ...prev,
        [key]: {
          ...prev[key],
          writeSuccess: true,
        },
      }));

      setTimeout(() => {
        setCharacteristicValues((prev) => ({
          ...prev,
          [key]: {
            ...prev[key],
            writeSuccess: false,
          },
        }));
      }, 2000);

      setLoading(false);
    } catch (err) {
      setError("Write failed: " + err.message);
      setLoading(false);
    }
  };

  const handleSubscribe = async (serviceUuid, charUuid, characteristic) => {
    try {
      setLoading(true);

      await characteristic.startNotifications();

      // Set up notification handler
      characteristic.addEventListener("characteristicvaluechanged", (event) => {
        const value = event.target.value;
        const hexString = Array.from(new Uint8Array(value.buffer))
          .map((b) => b.toString(16).padStart(2, "0"))
          .join(" ");

        const decoder = new TextDecoder("utf-8");
        let textValue = "";
        try {
          textValue = decoder.decode(value);
        } catch (e) {
          textValue = "(binary data)";
        }

        const key = `${serviceUuid}-${charUuid}`;
        setCharacteristicValues((prev) => ({
          ...prev,
          [key]: {
            hex: hexString,
            text: textValue,
            timestamp: new Date().toLocaleTimeString(),
            subscribed: true,
          },
        }));
      });

      const key = `${serviceUuid}-${charUuid}`;
      setCharacteristicValues((prev) => ({
        ...prev,
        [key]: {
          ...prev[key],
          subscribed: true,
        },
      }));

      setLoading(false);
    } catch (err) {
      setError("Subscribe failed: " + err.message);
      setLoading(false);
    }
  };

  // Helper to get service name from UUID
  const getServiceName = (uuid) => {
    const services = {
      "00001800-0000-1000-8000-00805f9b34fb": "Generic Access",
      "00001801-0000-1000-8000-00805f9b34fb": "Generic Attribute",
      "0000180a-0000-1000-8000-00805f9b34fb": "Device Information",
      "0000180f-0000-1000-8000-00805f9b34fb": "Battery Service",
      "00001805-0000-1000-8000-00805f9b34fb": "Current Time Service",
      "00001810-0000-1000-8000-00805f9b34fb": "Blood Pressure",
      "00001811-0000-1000-8000-00805f9b34fb": "Alert Notification Service",
      "00001812-0000-1000-8000-00805f9b34fb": "Human Interface Device",
      "00001816-0000-1000-8000-00805f9b34fb": "Cycling Speed and Cadence",
      "0000181a-0000-1000-8000-00805f9b34fb": "Environmental Sensing",
      "0000181c-0000-1000-8000-00805f9b34fb": "User Data",
      "0000181d-0000-1000-8000-00805f9b34fb": "Weight Scale",
    };
    return services[uuid] || `Custom Service`;
  };

  // Helper to get characteristic name from UUID
  const getCharacteristicName = (uuid) => {
    const characteristics = {
      "00002a00-0000-1000-8000-00805f9b34fb": "Device Name",
      "00002a01-0000-1000-8000-00805f9b34fb": "Appearance",
      "00002a19-0000-1000-8000-00805f9b34fb": "Battery Level",
      "00002a24-0000-1000-8000-00805f9b34fb": "Model Number",
      "00002a25-0000-1000-8000-00805f9b34fb": "Serial Number",
      "00002a26-0000-1000-8000-00805f9b34fb": "Firmware Revision",
      "00002a27-0000-1000-8000-00805f9b34fb": "Hardware Revision",
      "00002a28-0000-1000-8000-00805f9b34fb": "Software Revision",
      "00002a29-0000-1000-8000-00805f9b34fb": "Manufacturer Name",
      "00002a37-0000-1000-8000-00805f9b34fb": "Heart Rate Measurement",
      "00002a38-0000-1000-8000-00805f9b34fb": "Body Sensor Location",
    };
    return characteristics[uuid] || `Custom Characteristic`;
  };

  const getStatusChip = () => {
    const statusMap = {
      not_initialized: { label: "Not Initialized", color: "default" },
      initialized: { label: "Initialized", color: "info" },
      advertising: { label: "Advertising", color: "warning" },
      connected: { label: "Connected", color: "success" },
    };

    const status = bleStatus?.status || "not_initialized";
    const { label, color } = statusMap[status] || statusMap.not_initialized;

    return <Chip label={label} color={color} size="small" />;
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      maxWidth="md"
      fullWidth
      paperSx={{
        height: { xs: "90%", sm: "80vh" },
        maxHeight: { xs: "99%", sm: "80vh" },
      }}
      title={
        <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
          <BluetoothIcon color="primary" />
          Web Bluetooth
        </Box>
      }
    >
      <Box sx={{ p: 2, height: "100%", overflow: "auto" }}>
        {/* ESP32 BLE Status Section */}
        {deviceOnline && (
          <Paper sx={{ p: 2, mb: 2, bgcolor: "background.default" }}>
            <Typography variant="subtitle2" gutterBottom>
              {config?.deviceName || "ESP32"} BLE Advertising
            </Typography>
            <Box sx={{ display: "flex", alignItems: "center", gap: 2, mb: 1 }}>
              <Typography variant="body2">Status:</Typography>
              {getStatusChip()}
              {bleStatus?.address && (
                <Typography variant="caption" color="text.secondary">
                  {bleStatus.address}
                </Typography>
              )}
            </Box>

            <Button
              size="small"
              variant="outlined"
              color={isBLERunning ? "error" : "primary"}
              onClick={handleToggleESPBLE}
              disabled={!deviceOnline || loading}
              fullWidth
            >
              {isBLERunning
                ? `Stop ${config?.deviceName || "ESP32"} BLE`
                : `Start ${config?.deviceName || "ESP32"} BLE`}
            </Button>
          </Paper>
        )}

        {error && (
          <Alert severity="error" sx={{ mb: 2 }} onClose={() => setError(null)}>
            {error}
          </Alert>
        )}

        {!isWebBluetoothSupported() && (
          <Alert severity="warning" sx={{ mb: 2 }}>
            Web Bluetooth is not supported in this browser. Please use Chrome,
            Edge, or Opera on desktop or Android.
          </Alert>
        )}

        {/* Web Bluetooth Scanning Section */}
        {!connectedDevice && (
          <Paper sx={{ p: 2, mb: 2 }}>
            <Typography variant="h6" gutterBottom>
              Connect to BLE Device
            </Typography>
            <Typography variant="body2" color="text.secondary" paragraph>
              Click the button below to scan and connect to a BLE device using
              your browser.
            </Typography>

            <Button
              variant="contained"
              startIcon={
                loading ? <CircularProgress size={20} /> : <BluetoothIcon />
              }
              onClick={handleScan}
              disabled={loading || !isWebBluetoothSupported()}
              fullWidth
            >
              {loading ? "Connecting..." : "Scan for Devices"}
            </Button>
          </Paper>
        )}

        {/* Connected Device Section */}
        {connectedDevice && (
          <Paper sx={{ p: 2 }}>
            <Box
              sx={{
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
                mb: 2,
              }}
            >
              <Box>
                <Typography variant="h6">{connectedDevice.name}</Typography>
                <Typography variant="caption" color="text.secondary">
                  {connectedDevice.id}
                </Typography>
              </Box>
              <Button
                variant="outlined"
                color="error"
                onClick={handleDisconnect}
              >
                Disconnect
              </Button>
            </Box>

            <Divider sx={{ my: 2 }} />

            <Typography variant="subtitle1" gutterBottom>
              Services & Characteristics ({services.length})
            </Typography>

            {services.map((service, sIdx) => (
              <Paper key={sIdx} variant="outlined" sx={{ mb: 1 }}>
                <ListItemButton
                  onClick={() =>
                    setExpandedService(expandedService === sIdx ? null : sIdx)
                  }
                >
                  <ListItemText
                    primary={service.name}
                    secondary={service.uuid}
                    secondaryTypographyProps={{ sx: { fontSize: "0.75rem" } }}
                  />
                  {expandedService === sIdx ? (
                    <ExpandLessIcon />
                  ) : (
                    <ExpandMoreIcon />
                  )}
                </ListItemButton>

                <Collapse in={expandedService === sIdx}>
                  <Box sx={{ p: 2, bgcolor: "background.default" }}>
                    {service.characteristics.map((char, cIdx) => {
                      const key = `${service.uuid}-${char.uuid}`;
                      const charValue = characteristicValues[key];

                      return (
                        <Paper
                          key={cIdx}
                          sx={{ p: 2, mb: 1 }}
                          variant="outlined"
                        >
                          <Typography variant="body2" fontWeight="bold">
                            {char.name}
                          </Typography>
                          <Typography
                            variant="caption"
                            color="text.secondary"
                            display="block"
                            gutterBottom
                            sx={{ fontSize: "0.7rem", wordBreak: "break-all" }}
                          >
                            {char.uuid}
                          </Typography>

                          <Box
                            sx={{
                              display: "flex",
                              gap: 0.5,
                              flexWrap: "wrap",
                              mb: 1,
                            }}
                          >
                            {char.properties.map((prop) => (
                              <Chip
                                key={prop}
                                label={prop}
                                size="small"
                                variant="outlined"
                              />
                            ))}
                          </Box>

                          {/* Display read value */}
                          {charValue && (
                            <Box
                              sx={{
                                mb: 1,
                                p: 1,
                                bgcolor: "action.hover",
                                borderRadius: 1,
                              }}
                            >
                              <Typography
                                variant="caption"
                                color="text.secondary"
                              >
                                Last read: {charValue.timestamp}
                                {charValue.subscribed && " (subscribed)"}
                              </Typography>
                              <Typography
                                variant="body2"
                                fontFamily="monospace"
                              >
                                Hex: {charValue.hex}
                              </Typography>
                              <Typography variant="body2">
                                Text: {charValue.text}
                              </Typography>
                              {charValue.writeSuccess && (
                                <Chip
                                  label="Write successful!"
                                  color="success"
                                  size="small"
                                  sx={{ mt: 0.5 }}
                                />
                              )}
                            </Box>
                          )}

                          <Box
                            sx={{ display: "flex", gap: 1, flexWrap: "wrap" }}
                          >
                            {char.properties.includes("read") && (
                              <Button
                                size="small"
                                variant="outlined"
                                startIcon={<ReadIcon />}
                                onClick={() =>
                                  handleRead(
                                    service.uuid,
                                    char.uuid,
                                    char.characteristic
                                  )
                                }
                                disabled={loading}
                              >
                                Read
                              </Button>
                            )}

                            {(char.properties.includes("notify") ||
                              char.properties.includes("indicate")) && (
                              <Button
                                size="small"
                                variant={
                                  charValue?.subscribed
                                    ? "contained"
                                    : "outlined"
                                }
                                onClick={() =>
                                  handleSubscribe(
                                    service.uuid,
                                    char.uuid,
                                    char.characteristic
                                  )
                                }
                                disabled={loading || charValue?.subscribed}
                              >
                                {charValue?.subscribed
                                  ? "Subscribed"
                                  : "Subscribe"}
                              </Button>
                            )}

                            {(char.properties.includes("write") ||
                              char.properties.includes(
                                "write-without-response"
                              )) && (
                              <Box sx={{ display: "flex", gap: 1, flex: 1 }}>
                                <TextField
                                  size="small"
                                  placeholder="Value (text or hex: 01 02 03)"
                                  value={writeValue}
                                  onChange={(e) =>
                                    setWriteValue(e.target.value)
                                  }
                                  sx={{ flex: 1, minWidth: 150 }}
                                />
                                <Button
                                  size="small"
                                  variant="contained"
                                  startIcon={<WriteIcon />}
                                  onClick={() =>
                                    handleWrite(
                                      service.uuid,
                                      char.uuid,
                                      char.characteristic,
                                      writeValue
                                    )
                                  }
                                  disabled={!writeValue || loading}
                                >
                                  Write
                                </Button>
                              </Box>
                            )}
                          </Box>
                        </Paper>
                      );
                    })}
                  </Box>
                </Collapse>
              </Paper>
            ))}

            {services.length === 0 && !loading && (
              <Typography variant="body2" color="text.secondary" align="center">
                No services found
              </Typography>
            )}
          </Paper>
        )}
      </Box>
    </SettingsModal>
  );
}
