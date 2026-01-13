import React, { useState, useMemo } from "react";
import {
  Container,
  Box,
  Paper,
  Typography,
  Button,
  Alert,
  TextField,
  CircularProgress,
  IconButton,
  InputAdornment,
} from "@mui/material";
import BluetoothSearchingIcon from "@mui/icons-material/BluetoothSearching";
import CheckCircleIcon from "@mui/icons-material/CheckCircle";
import WifiIcon from "@mui/icons-material/Wifi";
import Visibility from "@mui/icons-material/Visibility";
import VisibilityOff from "@mui/icons-material/VisibilityOff";
import ClaimCodeEntry from "./ClaimCodeEntry";

const DIS_UUID = 0x180a;
const CONTROL_UUID = 0xfff1;

function hasWebBluetooth() {
  return Boolean(navigator?.bluetooth);
}

function encodeUtf8(s) {
  return new TextEncoder().encode(s);
}

function decodeUtf8(buf) {
  return new TextDecoder("utf-8").decode(buf);
}

async function writeJsonAndRead(characteristic, obj) {
  const payload = JSON.stringify(obj);

  // Redact sensitive fields for logging
  const safeObj = { ...obj };
  if (safeObj.password !== undefined) {
    safeObj.password = "***";
  }
  const safePayload = JSON.stringify(safeObj);

  console.log(
    "[BLE] Writing JSON payload:",
    safePayload,
    "length:",
    payload.length
  );

  await characteristic.writeValue(encodeUtf8(payload));

  const v = await characteristic.readValue();
  const txt = decodeUtf8(v.buffer);
  console.log("[BLE] Read response:", txt);

  return JSON.parse(txt);
}

/**
 * BLE Pairing Flow component
 * Streamlined flow to scan for and provision ESPWiFi devices via Bluetooth
 * Used for both initial setup and adding additional devices
 */
export default function BlePairingFlow({ onDeviceProvisioned, onClose }) {
  const supported = useMemo(() => hasWebBluetooth(), []);
  const [phase, setPhase] = useState("idle"); // idle | scanning | configuring | quick_connect | done
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [statusText, setStatusText] = useState("");

  // Claim code entry dialog
  const [showClaimEntry, setShowClaimEntry] = useState(false);

  // Device info after selection
  const [selectedDevice, setSelectedDevice] = useState(null);
  const [deviceIdentity, setDeviceIdentity] = useState(null);
  const [bleControl, setBleControl] = useState(null); // Keep BLE connection alive

  // WiFi credentials
  const [ssid, setSsid] = useState("");
  const [password, setPassword] = useState("");
  const [showPassword, setShowPassword] = useState(false);

  const handleStartScan = async () => {
    setError("");
    if (!supported) {
      setError("Web Bluetooth is not supported in this browser");
      return;
    }

    setBusy(true);
    setPhase("scanning");
    setStatusText("Opening Bluetooth device picker...");

    let dev = null;
    try {
      // Open Bluetooth device picker
      dev = await navigator.bluetooth.requestDevice({
        filters: [{ services: [DIS_UUID] }],
        optionalServices: [DIS_UUID],
      });

      setStatusText("Connecting to device...");
      const server = await dev.gatt.connect();
      const service = await server.getPrimaryService(DIS_UUID);
      const control = await service.getCharacteristic(CONTROL_UUID);

      setStatusText("Reading device information...");
      const identity = await writeJsonAndRead(control, { cmd: "get_identity" });

      // Keep BLE connection and control characteristic alive for configuration
      setSelectedDevice(dev);
      setDeviceIdentity(identity);
      setBleControl(control);

      // Check if device is already connected to WiFi and registered with cloud
      const hasCloudUrl = identity?.cloud?.enabled && identity?.cloud?.wsUrl;

      if (hasCloudUrl) {
        // Device is already configured and cloud-connected - offer quick connect
        setPhase("quick_connect");
      } else {
        // Device needs WiFi configuration
        setPhase("configuring");
      }

      setStatusText("");
      setBusy(false);
    } catch (e) {
      try {
        dev?.gatt?.disconnect?.();
      } catch {
        // ignore
      }
      setError(e?.message || String(e));
      setBusy(false);
      setPhase("idle");
      setStatusText("");
    }
  };

  const handleConfigure = async () => {
    setError("");

    // Validate SSID
    if (!ssid || !ssid.trim()) {
      setError("WiFi SSID is required");
      return;
    }

    if (!bleControl) {
      setError("BLE connection lost. Please scan again.");
      handleReset();
      return;
    }

    // Check if device is still connected
    if (!selectedDevice?.gatt?.connected) {
      setError("Device disconnected. Please scan again.");
      handleReset();
      return;
    }

    setBusy(true);
    setStatusText("Configuring WiFi client mode...");

    try {
      // Send WiFi credentials via BLE
      // Note: We intentionally do NOT send cloud configuration here.
      // The device reads cloud settings from DefaultConfig.cpp (cloud.enabled = true by default).
      // This keeps the device autonomous and reduces BLE complexity.
      const payload = {
        cmd: "set_wifi",
        ssid: ssid.trim(),
        password: password || "",
      };

      console.log("[InitialSetup] Sending WiFi config:", {
        ...payload,
        password: password ? "***" : "(empty)",
      });

      const result = await writeJsonAndRead(bleControl, payload);

      console.log("[InitialSetup] Received response:", result);

      // Check if successful
      if (result?.ok === false) {
        const errorMsg = result?.error || "Failed to configure WiFi";
        console.error("[InitialSetup] Config failed:", errorMsg);
        throw new Error(errorMsg);
      }

      setStatusText(
        "Configuration saved, device will restart automatically..."
      );

      // Device will restart WiFi automatically - give it time
      await new Promise((r) => setTimeout(r, 3000));

      // Close BLE connection
      try {
        selectedDevice?.gatt?.disconnect?.();
      } catch {
        // ignore
      }

      // Create device record
      const deviceId =
        deviceIdentity?.hostname ||
        deviceIdentity?.deviceId ||
        selectedDevice?.name ||
        selectedDevice?.id;
      const id = String(deviceId);

      // Use cloud config from device if available
      const cloudConfig = deviceIdentity?.cloud?.enabled
        ? {
            enabled: true,
            baseUrl: deviceIdentity.cloud.baseUrl,
            tunnel: deviceIdentity.cloud.tunnel,
            wsUrl: deviceIdentity.cloud.wsUrl || null, // Full UI WebSocket URL from device
          }
        : { enabled: false };

      const record = {
        id,
        name:
          deviceIdentity?.deviceName ||
          deviceIdentity?.model ||
          selectedDevice?.name ||
          String(id),
        hostname: deviceIdentity?.hostname || null,
        deviceId: String(deviceId),
        authToken: deviceIdentity?.token || null,
        cloudTunnel: cloudConfig,
        lastSeenAtMs: Date.now(),
      };

      setPhase("done");
      setStatusText("Device configured successfully!");
      setBusy(false);

      // Notify parent
      setTimeout(() => {
        onDeviceProvisioned?.(record, {
          identity: deviceIdentity,
        });
      }, 1000);
    } catch (e) {
      try {
        selectedDevice?.gatt?.disconnect?.();
      } catch {
        // ignore
      }
      setError(e?.message || String(e));
      setBusy(false);
      setStatusText("");
    }
  };

  const handleReset = () => {
    // Disconnect BLE if still connected
    try {
      selectedDevice?.gatt?.disconnect?.();
    } catch {
      // ignore
    }

    setPhase("idle");
    setSelectedDevice(null);
    setDeviceIdentity(null);
    setBleControl(null);
    setSsid("");
    setPassword("");
    setError("");
    setStatusText("");

    // If onClose is provided, call it when canceling
    if (onClose) {
      onClose();
    }
  };

  const handleQuickConnect = async () => {
    setError("");
    setBusy(true);
    setStatusText("Saving device to your dashboard...");

    try {
      // Close BLE connection
      try {
        selectedDevice?.gatt?.disconnect?.();
      } catch {
        // ignore
      }

      // Create device record with cloud info
      const deviceId =
        deviceIdentity?.hostname ||
        deviceIdentity?.deviceId ||
        selectedDevice?.name ||
        selectedDevice?.id;
      const id = String(deviceId);

      const cloudConfig = {
        enabled: true,
        baseUrl: deviceIdentity.cloud.baseUrl,
        tunnel: deviceIdentity.cloud.tunnel,
        wsUrl: deviceIdentity.cloud.wsUrl,
      };

      const record = {
        id,
        name: deviceIdentity?.deviceName || selectedDevice?.name || String(id),
        hostname: deviceIdentity?.hostname || null,
        deviceId: String(deviceId),
        authToken: deviceIdentity?.token || null,
        cloudTunnel: cloudConfig,
        lastSeenAtMs: Date.now(),
      };

      setPhase("done");
      setStatusText("Device added successfully!");
      setBusy(false);

      // Notify parent
      setTimeout(() => {
        onDeviceProvisioned?.(record, {
          identity: deviceIdentity,
        });
      }, 1000);
    } catch (e) {
      try {
        selectedDevice?.gatt?.disconnect?.();
      } catch {
        // ignore
      }
      setError(e?.message || String(e));
      setBusy(false);
      setStatusText("");
    }
  };

  const handleClaimCodeSuccess = (deviceRecord) => {
    console.log("[BlePairingFlow] Device claimed:", deviceRecord);
    setShowClaimEntry(false);

    // Notify parent and close
    onDeviceProvisioned?.(deviceRecord, {
      viaClaim: true,
    });

    // Show success message
    setPhase("done");
    setStatusText("Device claimed successfully!");
  };

  return (
    <Container
      maxWidth="sm"
      sx={{
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        justifyContent: "center",
        minHeight: "80vh",
        gap: 3,
      }}
    >
      <Paper
        elevation={3}
        sx={{
          p: 4,
          width: "100%",
          display: "flex",
          flexDirection: "column",
          gap: 3,
          alignItems: "center",
        }}
      >
        {/* Icon changes based on phase */}
        {phase === "idle" && (
          <BluetoothSearchingIcon
            sx={{ fontSize: 80, color: "primary.main", opacity: 0.9 }}
          />
        )}
        {phase === "scanning" && (
          <BluetoothSearchingIcon
            sx={{ fontSize: 80, color: "primary.main", opacity: 0.9 }}
          />
        )}
        {(phase === "configuring" || phase === "quick_connect") && (
          <WifiIcon
            sx={{ fontSize: 80, color: "primary.main", opacity: 0.9 }}
          />
        )}
        {phase === "done" && (
          <CheckCircleIcon
            sx={{ fontSize: 80, color: "success.main", opacity: 0.9 }}
          />
        )}

        <Typography
          variant="h4"
          sx={{
            fontWeight: 700,
            textAlign: "center",
            fontFamily: "headerFontFamily",
          }}
        >
          {phase === "idle" && "Welcome to ESPWiFi"}
          {phase === "scanning" && "Selecting Device"}
          {phase === "configuring" && "Configure WiFi"}
          {phase === "quick_connect" && "Device Already Configured"}
          {phase === "done" && "Setup Complete!"}
        </Typography>

        {/* IDLE PHASE - Initial welcome */}
        {phase === "idle" && (
          <>
            <Typography
              variant="body1"
              sx={{ textAlign: "center", opacity: 0.9, maxWidth: "400px" }}
            >
              Connect to your ESPWiFi device using Bluetooth for initial setup,
              or enter a claim code to pair an already-configured device.
            </Typography>

            <Box
              sx={{
                width: "100%",
                display: "flex",
                flexDirection: "column",
                gap: 2,
              }}
            >
              {!supported && (
                <Alert severity="warning" sx={{ mb: 2 }}>
                  Web Bluetooth isn't available in this browser. Use Chrome on
                  Android or Desktop Chrome for BLE pairing.
                </Alert>
              )}

              <Button
                variant="contained"
                size="large"
                fullWidth
                onClick={handleStartScan}
                disabled={!supported || busy}
                startIcon={<BluetoothSearchingIcon />}
                sx={{
                  py: 1.5,
                  fontSize: "1.1rem",
                  fontWeight: 600,
                }}
              >
                Scan for Devices
              </Button>

              <Button
                variant="outlined"
                size="large"
                fullWidth
                onClick={() => setShowClaimEntry(true)}
                disabled={busy}
                sx={{
                  py: 1.5,
                  fontSize: "1.1rem",
                  fontWeight: 600,
                }}
              >
                Enter Claim Code
              </Button>

              {onClose && (
                <Button
                  variant="text"
                  size="large"
                  fullWidth
                  onClick={onClose}
                  sx={{
                    py: 1.5,
                    fontSize: "1.1rem",
                    fontWeight: 600,
                  }}
                >
                  Skip for Now
                </Button>
              )}

              <Alert severity="info" sx={{ textAlign: "left" }}>
                <Typography variant="body2" sx={{ fontWeight: 600, mb: 0.5 }}>
                  Bluetooth pairing requirements:
                </Typography>
                <Typography variant="body2" component="ul" sx={{ m: 0, pl: 2 }}>
                  <li>ESPWiFi device powered on and in AP mode</li>
                  <li>Bluetooth enabled on this device</li>
                  <li>Chrome, Edge, or Bluetooth-compatible browser</li>
                </Typography>
              </Alert>
            </Box>
          </>
        )}

        {/* CONFIGURING PHASE - WiFi credentials */}
        {phase === "configuring" && (
          <>
            <Alert severity="success" sx={{ width: "100%", mb: 1 }}>
              <Typography variant="body2" sx={{ fontWeight: 600 }}>
                Device selected:{" "}
                {deviceIdentity?.deviceName ||
                  selectedDevice?.name ||
                  "Unknown"}
              </Typography>
            </Alert>

            <Typography
              variant="body1"
              sx={{ textAlign: "center", opacity: 0.9, maxWidth: "400px" }}
            >
              Enter your WiFi network credentials. The device will switch to
              client mode and restart to connect to your network.
            </Typography>

            <Box
              sx={{
                width: "100%",
                display: "flex",
                flexDirection: "column",
                gap: 2,
              }}
            >
              <TextField
                label="WiFi SSID"
                value={ssid}
                onChange={(e) => setSsid(e.target.value)}
                autoComplete="off"
                fullWidth
                autoFocus
                disabled={busy}
                required
                error={!ssid && error.includes("SSID")}
                helperText={
                  !ssid && error.includes("SSID") ? "SSID is required" : ""
                }
              />
              <TextField
                label="WiFi Password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                type={showPassword ? "text" : "password"}
                autoComplete="off"
                fullWidth
                disabled={busy}
                InputProps={{
                  endAdornment: (
                    <InputAdornment position="end">
                      <IconButton
                        aria-label="toggle password visibility"
                        onClick={() => setShowPassword(!showPassword)}
                        onMouseDown={(e) => e.preventDefault()}
                        edge="end"
                        disabled={busy}
                      >
                        {showPassword ? <VisibilityOff /> : <Visibility />}
                      </IconButton>
                    </InputAdornment>
                  ),
                }}
              />

              <Box sx={{ display: "flex", gap: 1, mt: 1 }}>
                <Button
                  variant="outlined"
                  onClick={handleReset}
                  disabled={busy}
                  sx={{ flex: 1 }}
                >
                  Cancel
                </Button>
                <Button
                  variant="contained"
                  size="large"
                  onClick={handleConfigure}
                  disabled={busy}
                  sx={{ flex: 2, py: 1.5, fontWeight: 600 }}
                >
                  {busy ? "Configuring..." : "Configure & Restart"}
                </Button>
              </Box>
            </Box>
          </>
        )}

        {/* QUICK CONNECT PHASE - Device already configured */}
        {phase === "quick_connect" && (
          <>
            <Alert severity="success" sx={{ width: "100%", mb: 1 }}>
              <Typography variant="body2" sx={{ fontWeight: 600 }}>
                Device found:{" "}
                {deviceIdentity?.deviceName ||
                  selectedDevice?.name ||
                  "Unknown"}
              </Typography>
            </Alert>

            <Alert severity="info" sx={{ width: "100%", mb: 2 }}>
              <Typography variant="body2">
                This device is already connected to WiFi and registered with the
                cloud.
              </Typography>
            </Alert>

            <Typography
              variant="body1"
              sx={{ textAlign: "center", opacity: 0.9, maxWidth: "400px" }}
            >
              Would you like to add this device to your dashboard and connect
              via cloud?
            </Typography>

            <Box
              sx={{
                width: "100%",
                display: "flex",
                flexDirection: "column",
                gap: 2,
                mt: 2,
              }}
            >
              <Button
                variant="contained"
                size="large"
                fullWidth
                onClick={handleQuickConnect}
                disabled={busy}
                sx={{
                  py: 1.5,
                  fontSize: "1.1rem",
                  fontWeight: 600,
                }}
              >
                {busy ? "Adding Device..." : "Connect to Cloud"}
              </Button>

              <Button
                variant="outlined"
                size="large"
                fullWidth
                onClick={() => setPhase("configuring")}
                disabled={busy}
                sx={{
                  py: 1.5,
                  fontSize: "1.1rem",
                  fontWeight: 600,
                }}
              >
                Reconfigure WiFi Instead
              </Button>

              <Button
                variant="text"
                fullWidth
                onClick={handleReset}
                disabled={busy}
                sx={{ py: 1 }}
              >
                Cancel
              </Button>
            </Box>
          </>
        )}

        {/* DONE PHASE - Success message */}
        {phase === "done" && (
          <>
            <Typography
              variant="body1"
              sx={{ textAlign: "center", opacity: 0.9, maxWidth: "400px" }}
            >
              Your ESPWiFi device has been configured and is connecting to your
              WiFi network.
            </Typography>

            {onClose && (
              <Button
                variant="contained"
                size="large"
                fullWidth
                onClick={onClose}
                sx={{
                  py: 1.5,
                  fontSize: "1.1rem",
                  fontWeight: 600,
                  mt: 2,
                }}
              >
                Continue to Dashboard
              </Button>
            )}
          </>
        )}

        {/* Status/Error messages */}
        {busy && statusText && (
          <Box
            sx={{
              display: "flex",
              alignItems: "center",
              gap: 1,
              width: "100%",
            }}
          >
            <CircularProgress size={18} />
            <Typography variant="body2" sx={{ opacity: 0.9 }}>
              {statusText}
            </Typography>
          </Box>
        )}

        {error && (
          <Alert severity="error" sx={{ width: "100%" }}>
            {error}
          </Alert>
        )}
      </Paper>

      {phase === "idle" && (
        <Typography
          variant="caption"
          sx={{ opacity: 0.6, textAlign: "center" }}
        >
          Once configured, your device will connect to WiFi.
        </Typography>
      )}

      {/* Claim Code Entry Dialog */}
      <ClaimCodeEntry
        open={showClaimEntry}
        onClose={() => setShowClaimEntry(false)}
        onDeviceClaimed={handleClaimCodeSuccess}
        existingDevices={[]}
      />
    </Container>
  );
}
