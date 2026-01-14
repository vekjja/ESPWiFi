import React, { useState, useEffect } from "react";
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
  IconButton,
  InputAdornment,
} from "@mui/material";
import BluetoothIcon from "@mui/icons-material/Bluetooth";
import BluetoothSearchingIcon from "@mui/icons-material/BluetoothSearching";
import ExpandMoreIcon from "@mui/icons-material/ExpandMore";
import ExpandLessIcon from "@mui/icons-material/ExpandLess";
import ReadIcon from "@mui/icons-material/Visibility";
import WriteIcon from "@mui/icons-material/Edit";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import SettingsModal from "./SettingsModal";

export default function BluetoothSettingsModal({
  open,
  onClose,
  config,
  saveConfig,
  saveConfigToDevice,
  deviceOnline,
}) {
  const [connectedDevice, setConnectedDevice] = useState(null);
  const [services, setServices] = useState([]);
  const [expandedService, setExpandedService] = useState(null);
  const [error, setError] = useState(null);
  const [writeValue, setWriteValue] = useState("");
  const [loading, setLoading] = useState(false);
  const [characteristicValues, setCharacteristicValues] = useState({});
  const [notificationLog, setNotificationLog] = useState([]);
  const [pairedDevices, setPairedDevices] = useState([]);
  const [showPassword, setShowPassword] = useState(false);

  // ESPWiFi Control Characteristic UUID
  const ESPWIFI_CONTROL_UUID = "0000fff1-0000-1000-8000-00805f9b34fb";
  const ESPWIFI_SERVICE_UUID = "0000180a-0000-1000-8000-00805f9b34fb";

  // Load paired devices from localStorage on mount
  useEffect(() => {
    try {
      const stored = localStorage.getItem("bleConnectedDevices");
      if (stored) {
        const devices = JSON.parse(stored);
        setPairedDevices(devices);
      }
    } catch (err) {
      console.error("Failed to load paired devices:", err);
    }
  }, []);

  // Save device to paired list
  const savePairedDevice = (device) => {
    try {
      const deviceInfo = {
        id: device.id,
        name: device.name || "Unknown Device",
        lastConnected: Date.now(),
      };

      setPairedDevices((prev) => {
        const filtered = prev.filter((d) => d.id !== device.id);
        const updated = [deviceInfo, ...filtered].slice(0, 10); // Keep last 10
        localStorage.setItem("bleConnectedDevices", JSON.stringify(updated));
        return updated;
      });
    } catch (err) {
      console.error("Failed to save paired device:", err);
    }
  };

  // Remove device from paired list
  const removePairedDevice = (deviceId) => {
    setPairedDevices((prev) => {
      const updated = prev.filter((d) => d.id !== deviceId);
      localStorage.setItem("bleConnectedDevices", JSON.stringify(updated));
      return updated;
    });
  };

  // Handle ESPWiFi-specific commands
  const handleESPWiFiCommand = async (cmd, params) => {
    setError(null);
    setLoading(true);

    try {
      // Find the ESPWiFi control characteristic
      const espWifiService = services.find(
        (s) => s.uuid === ESPWIFI_SERVICE_UUID
      );
      if (!espWifiService) {
        throw new Error("ESPWiFi service not found");
      }

      const controlChar = espWifiService.characteristics.find(
        (c) => c.uuid === ESPWIFI_CONTROL_UUID
      );
      if (!controlChar) {
        throw new Error("ESPWiFi control characteristic not found");
      }

      // Build the command JSON
      const command = { cmd, ...params };
      const payload = JSON.stringify(command);
      console.log("[ESPWiFi] Sending command:", payload);

      // Write the command
      const encoder = new TextEncoder();
      await controlChar.characteristic.writeValue(encoder.encode(payload));

      // Read the response (for characteristics that support read)
      if (controlChar.properties.includes("read")) {
        const response = await controlChar.characteristic.readValue();
        const decoder = new TextDecoder("utf-8");
        const responseText = decoder.decode(response);
        console.log("[ESPWiFi] Response:", responseText);

        try {
          const responseJson = JSON.parse(responseText);

          // Add to notification log
          setNotificationLog((prev) => [
            {
              timestamp: new Date().toLocaleTimeString(),
              data: responseJson,
              raw: responseText,
            },
            ...prev.slice(0, 9),
          ]);

          // Update characteristic values display
          const key = `${ESPWIFI_SERVICE_UUID}-${ESPWIFI_CONTROL_UUID}`;
          setCharacteristicValues((prev) => ({
            ...prev,
            [key]: {
              hex: Array.from(new Uint8Array(response.buffer))
                .map((b) => b.toString(16).padStart(2, "0"))
                .join(" "),
              text: responseText,
              timestamp: new Date().toLocaleTimeString(),
            },
          }));

          if (!responseJson.ok) {
            setError(responseJson.error || "Command failed");
          }
        } catch (e) {
          setError("Failed to parse response JSON");
        }
      }

      // If characteristic supports notify, wait for notification
      if (controlChar.properties.includes("notify")) {
        // Response will come via notification handler
        console.log("[ESPWiFi] Waiting for notification response...");
      }

      setLoading(false);
    } catch (err) {
      setError("Command failed: " + err.message);
      setLoading(false);
    }
  };

  // Check if Web Bluetooth is supported
  const isWebBluetoothSupported = () => {
    return navigator.bluetooth !== undefined;
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
          ESPWIFI_SERVICE_UUID,
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

  const handleConnectToPaired = async (pairedDevice) => {
    setError("");
    setLoading(true);

    try {
      // Try to get previously authorized devices (Chrome 85+)
      if (navigator.bluetooth.getDevices) {
        const devices = await navigator.bluetooth.getDevices();
        console.log("[BLE] Found authorized devices:", devices);

        const device = devices.find(
          (d) => d.id === pairedDevice.id || d.name === pairedDevice.name
        );

        if (device) {
          console.log(
            "[BLE] Found matching device, connecting directly:",
            device.name
          );

          // If already connected, disconnect first to ensure clean reconnection
          if (device.gatt && device.gatt.connected) {
            console.log(
              "[BLE] Device already connected, using existing connection"
            );
            await connectToDevice(device);
            return;
          }

          // Connect directly without picker
          try {
            await connectToDevice(device);
            console.log("[BLE] Successfully reconnected without picker");
            return;
          } catch (connectErr) {
            console.error("[BLE] Direct connection failed:", connectErr);
            // Don't fall through to picker, just show error
            throw connectErr;
          }
        } else {
          console.log(
            "[BLE] Device not in authorized list (ID:",
            pairedDevice.id,
            "Name:",
            pairedDevice.name,
            ")"
          );
        }
      } else {
        console.log("[BLE] getDevices() not supported, will show picker");
      }

      // Fallback: If getDevices not available or device not found, show picker
      console.log("[BLE] Opening device picker as fallback...");
      const device = await navigator.bluetooth.requestDevice({
        acceptAllDevices: true,
        optionalServices: [
          "generic_access",
          "device_information",
          "battery_service",
          ESPWIFI_SERVICE_UUID,
        ],
      });

      console.log("[BLE] Selected device from picker:", device.name);
      await connectToDevice(device);
    } catch (err) {
      if (err.name === "NotFoundError") {
        setError("No device selected");
      } else {
        setError("Connection failed: " + err.message);
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

        // Auto-subscribe to ESPWiFi control characteristic notifications
        if (service.uuid === ESPWIFI_SERVICE_UUID) {
          const controlChar = characteristics.find(
            (c) => c.uuid === ESPWIFI_CONTROL_UUID
          );
          if (controlChar && controlChar.properties.notify) {
            try {
              await controlChar.startNotifications();
              console.log(
                "[ESPWiFi] Subscribed to control characteristic notifications"
              );

              controlChar.addEventListener(
                "characteristicvaluechanged",
                (event) => {
                  const value = event.target.value;
                  const decoder = new TextDecoder("utf-8");
                  const text = decoder.decode(value);

                  console.log("[ESPWiFi] Notification received:", text);

                  try {
                    const response = JSON.parse(text);

                    // Add to notification log
                    setNotificationLog((prev) => [
                      {
                        timestamp: new Date().toLocaleTimeString(),
                        data: response,
                        raw: text,
                      },
                      ...prev.slice(0, 9), // Keep last 10 notifications
                    ]);

                    // Update display
                    const key = `${ESPWIFI_SERVICE_UUID}-${ESPWIFI_CONTROL_UUID}`;
                    setCharacteristicValues((prev) => ({
                      ...prev,
                      [key]: {
                        hex: Array.from(new Uint8Array(value.buffer))
                          .map((b) => b.toString(16).padStart(2, "0"))
                          .join(" "),
                        text: text,
                        timestamp: new Date().toLocaleTimeString(),
                        subscribed: true,
                      },
                    }));
                  } catch (e) {
                    console.error("[ESPWiFi] Failed to parse notification:", e);
                    // Log unparseable notifications too
                    setNotificationLog((prev) => [
                      {
                        timestamp: new Date().toLocaleTimeString(),
                        data: null,
                        raw: text,
                        error: "Failed to parse JSON",
                      },
                      ...prev.slice(0, 9),
                    ]);
                  }
                }
              );
            } catch (err) {
              console.error(
                "[ESPWiFi] Failed to subscribe to notifications:",
                err
              );
            }
          }
        }
      }

      setConnectedDevice({
        name: device.name || "Unknown Device",
        id: device.id,
        device: device,
        server: server,
      });
      setServices(servicesData);

      // Save to paired devices list
      savePairedDevice(device);

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
    setNotificationLog([]);
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
      "0000fff1-0000-1000-8000-00805f9b34fb": "ESPWiFi Control",
    };
    return characteristics[uuid] || `Custom Characteristic`;
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
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 1,
            width: "100%",
          }}
        >
          <BluetoothIcon color="primary" />
          Web Bluetooth
        </Box>
      }
    >
      <Box sx={{ p: 2, height: "100%", overflow: "auto" }}>
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
              Scan for and connect to BLE devices using Web Bluetooth. View GATT
              services, read characteristics, and interact with BLE devices.
            </Typography>

            <Button
              variant="contained"
              size="large"
              startIcon={
                loading ? (
                  <CircularProgress size={20} />
                ) : (
                  <BluetoothSearchingIcon />
                )
              }
              onClick={handleScan}
              disabled={loading || !isWebBluetoothSupported()}
              fullWidth
            >
              {loading ? "Connecting..." : "Scan BT Devices"}
            </Button>

            {/* Paired Devices List */}
            {pairedDevices.length > 0 && (
              <Box sx={{ mt: 2 }}>
                <Divider sx={{ my: 2 }} />
                <Typography variant="body2" fontWeight="bold" gutterBottom>
                  Previously Paired Devices
                </Typography>
                {pairedDevices.map((device) => (
                  <Paper
                    key={device.id}
                    variant="outlined"
                    sx={{
                      p: 1.5,
                      mb: 1,
                      display: "flex",
                      justifyContent: "space-between",
                      alignItems: "center",
                    }}
                  >
                    <Box>
                      <Typography variant="body2" fontWeight="bold">
                        {device.name}
                      </Typography>
                      <Typography variant="caption" color="text.secondary">
                        Last connected:{" "}
                        {new Date(device.lastConnected).toLocaleString()}
                      </Typography>
                    </Box>
                    <Box sx={{ display: "flex", gap: 1 }}>
                      <Button
                        size="small"
                        variant="outlined"
                        onClick={() => handleConnectToPaired(device)}
                        disabled={loading}
                      >
                        Connect
                      </Button>
                      <Button
                        size="small"
                        variant="text"
                        color="error"
                        onClick={() => removePairedDevice(device.id)}
                      >
                        Remove
                      </Button>
                    </Box>
                  </Paper>
                ))}
              </Box>
            )}
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

            {/* ESPWiFi Specialized Controls */}
            {services.some(
              (s) => s.uuid === "0000180a-0000-1000-8000-00805f9b34fb"
            ) && (
              <>
                <Typography
                  variant="subtitle1"
                  gutterBottom
                  sx={{ fontWeight: 600 }}
                >
                  ESPWiFi Controls
                </Typography>

                {/* Notification Log */}
                {notificationLog.length > 0 && (
                  <Paper sx={{ p: 2, mb: 2, bgcolor: "action.hover" }}>
                    <Typography variant="body2" fontWeight="bold" gutterBottom>
                      Notifications ({notificationLog.length})
                      <Button
                        size="small"
                        onClick={() => setNotificationLog([])}
                        sx={{ ml: 1, minWidth: "auto" }}
                      >
                        Clear
                      </Button>
                    </Typography>
                    <Box
                      sx={{
                        maxHeight: 300,
                        overflow: "auto",
                        display: "flex",
                        flexDirection: "column",
                        gap: 1,
                      }}
                    >
                      {notificationLog.map((log, idx) => (
                        <Paper
                          key={idx}
                          variant="outlined"
                          sx={{
                            p: 1.5,
                            bgcolor: log.error ? "error.dark" : "success.dark",
                            opacity: 0.95,
                          }}
                        >
                          <Typography
                            variant="caption"
                            sx={{
                              fontSize: "0.7rem",
                              color: "grey.300",
                              fontWeight: 500,
                            }}
                          >
                            {log.timestamp}
                          </Typography>
                          <Typography
                            variant="caption"
                            component="pre"
                            sx={{
                              fontFamily: "monospace",
                              whiteSpace: "pre",
                              overflow: "auto",
                              fontSize: "0.75rem",
                              mt: 0.5,
                              display: "block",
                              lineHeight: 1.5,
                              textAlign: "left",
                            }}
                          >
                            {log.error
                              ? `ERROR: ${log.error}\n${log.raw}`
                              : JSON.stringify(log.data, null, 2)}
                          </Typography>
                        </Paper>
                      ))}
                    </Box>
                  </Paper>
                )}

                {/* WiFi Configuration */}
                <Paper sx={{ p: 2, mb: 2, bgcolor: "action.hover" }}>
                  <Typography variant="body2" fontWeight="bold" gutterBottom>
                    Configure WiFi
                  </Typography>
                  <Box
                    sx={{ display: "flex", flexDirection: "column", gap: 1.5 }}
                  >
                    <TextField
                      label="WiFi SSID"
                      size="small"
                      fullWidth
                      value={writeValue}
                      onChange={(e) => setWriteValue(e.target.value)}
                      placeholder="MyNetwork"
                      disabled={loading}
                    />
                    <TextField
                      label="WiFi Password"
                      size="small"
                      type={showPassword ? "text" : "password"}
                      fullWidth
                      placeholder="password"
                      disabled={loading}
                      id="wifi-password-field"
                      onKeyPress={(e) => {
                        if (e.key === "Enter") {
                          const password = e.target.value;
                          handleESPWiFiCommand("set_wifi", {
                            ssid: writeValue,
                            password,
                          });
                        }
                      }}
                      InputProps={{
                        endAdornment: (
                          <InputAdornment position="end">
                            <IconButton
                              aria-label="toggle password visibility"
                              onClick={() => setShowPassword(!showPassword)}
                              onMouseDown={(e) => e.preventDefault()}
                              edge="end"
                              size="small"
                            >
                              {showPassword ? (
                                <VisibilityOffIcon fontSize="small" />
                              ) : (
                                <VisibilityIcon fontSize="small" />
                              )}
                            </IconButton>
                          </InputAdornment>
                        ),
                      }}
                    />
                    <Button
                      variant="contained"
                      size="small"
                      onClick={() => {
                        const ssid = writeValue;
                        const password =
                          document.getElementById("wifi-password-field")
                            ?.value || "";
                        handleESPWiFiCommand("set_wifi", { ssid, password });
                      }}
                      disabled={!writeValue || loading}
                      fullWidth
                    >
                      {loading ? "Sending..." : "Set WiFi Credentials"}
                    </Button>
                  </Box>
                </Paper>

                {/* Get Info */}
                <Paper sx={{ p: 2, mb: 2, bgcolor: "action.hover" }}>
                  <Typography variant="body2" fontWeight="bold" gutterBottom>
                    Device Information
                  </Typography>
                  <Button
                    variant="outlined"
                    size="small"
                    onClick={() => handleESPWiFiCommand("get_info", {})}
                    disabled={loading}
                    fullWidth
                  >
                    {loading ? "Loading..." : "Get Device Info"}
                  </Button>
                </Paper>

                <Divider sx={{ my: 2 }} />
              </>
            )}

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
