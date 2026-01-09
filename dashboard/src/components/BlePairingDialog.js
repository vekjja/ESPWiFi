import React, { useMemo, useState } from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  Typography,
  Box,
  Alert,
  TextField,
  CircularProgress,
} from "@mui/material";

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
  await characteristic.writeValue(encodeUtf8(payload));
  const v = await characteristic.readValue();
  const txt = decodeUtf8(v.buffer);
  return JSON.parse(txt);
}

export default function BlePairingDialog({ open, onClose, onPaired }) {
  const supported = useMemo(() => hasWebBluetooth(), []);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [ssid, setSsid] = useState("");
  const [password, setPassword] = useState("");
  const [phase, setPhase] = useState("idle"); // idle | scan | wifi | connecting | done
  const [statusText, setStatusText] = useState("");

  const handlePair = async () => {
    setError("");
    if (!supported) return;
    setBusy(true);
    setPhase("scan");
    setStatusText("");
    let dev = null;
    try {
      dev = await navigator.bluetooth.requestDevice({
        filters: [{ services: [DIS_UUID] }],
        optionalServices: [DIS_UUID],
      });

      const server = await dev.gatt.connect();
      const service = await server.getPrimaryService(DIS_UUID);
      const control = await service.getCharacteristic(CONTROL_UUID);

      setStatusText("Reading device info…");
      const identity = await writeJsonAndRead(control, { cmd: "get_identity" });

      // If device is in AP / not online yet, we need to provide WiFi creds.
      setStatusText("Checking WiFi status…");
      const status = await writeJsonAndRead(control, { cmd: "get_status" });
      const ip = status?.ip || "0.0.0.0";
      const wifiMode = (status?.wifiMode || "").toLowerCase();
      const needsWifi =
        ip === "0.0.0.0" || wifiMode === "ap" || wifiMode === "accesspoint";

      if (needsWifi) {
        // Defer to the WiFi credentials phase; keep BLE connected while dialog stays open.
        setPhase("wifi");
        setBusy(false);
        setStatusText("");
        return;
      }

      setStatusText("Enabling cloud tunnel…");
      await writeJsonAndRead(control, {
        cmd: "set_cloud",
        enabled: true,
        baseUrl: "https://tnl.espwifi.io",
      });

      setStatusText("Fetching cloud tunnel details…");
      const cloud = await writeJsonAndRead(control, { cmd: "get_cloud" });

      // Close BLE to avoid keeping the device connected.
      try {
        dev?.gatt?.disconnect?.();
      } catch {
        // ignore
      }

      const deviceId =
        cloud?.deviceId || identity?.hostname || identity?.deviceId || null;
      const id = deviceId || identity?.hostname || dev?.name || dev?.id;
      if (!id) throw new Error("BLE device did not provide an id/hostname");

      const record = {
        id: String(id),
        name:
          identity?.deviceName || identity?.model || dev?.name || String(id),
        hostname: identity?.hostname || null,
        deviceId: deviceId || String(id),
        authToken: cloud?.token || identity?.token || null,
        cloudBaseUrl: cloud?.baseUrl || "https://tnl.espwifi.io",
        lastSeenAtMs: Date.now(),
      };

      onPaired?.(record, { identity, cloud });
      onClose?.();
    } catch (e) {
      try {
        dev?.gatt?.disconnect?.();
      } catch {
        // ignore
      }
      setError(e?.message || String(e));
    } finally {
      setBusy(false);
      setPhase("idle");
      setStatusText("");
    }
  };

  const handleProvisionWifi = async () => {
    setError("");
    if (!supported) return;
    if (!ssid.trim()) {
      setError("SSID is required");
      return;
    }
    setBusy(true);
    setPhase("connecting");
    setStatusText("Sending WiFi credentials…");
    let dev = null;
    try {
      // We need to re-open the chooser because we intentionally returned early
      // from handlePair() (browsers don't allow keeping the device object across
      // arbitrary dialog lifetimes reliably).
      dev = await navigator.bluetooth.requestDevice({
        filters: [{ services: [DIS_UUID] }],
        optionalServices: [DIS_UUID],
      });
      const server = await dev.gatt.connect();
      const service = await server.getPrimaryService(DIS_UUID);
      const control = await service.getCharacteristic(CONTROL_UUID);

      const identity = await writeJsonAndRead(control, { cmd: "get_identity" });
      await writeJsonAndRead(control, {
        cmd: "set_wifi",
        ssid: ssid.trim(),
        password,
      });

      // Poll for IP
      const deadline = Date.now() + 45_000;
      let ip = "0.0.0.0";
      while (Date.now() < deadline) {
        setStatusText("Waiting for device to connect to WiFi…");
        const st = await writeJsonAndRead(control, { cmd: "get_status" });
        ip = st?.ip || "0.0.0.0";
        if (ip && ip !== "0.0.0.0") break;
        await new Promise((r) => setTimeout(r, 2000));
      }

      if (!ip || ip === "0.0.0.0") {
        throw new Error("Timed out waiting for WiFi connection");
      }

      setStatusText("Enabling cloud tunnel…");
      await writeJsonAndRead(control, {
        cmd: "set_cloud",
        enabled: true,
        baseUrl: "https://tnl.espwifi.io",
      });

      setStatusText("Fetching cloud tunnel details…");
      const cloud = await writeJsonAndRead(control, { cmd: "get_cloud" });

      try {
        dev?.gatt?.disconnect?.();
      } catch {
        // ignore
      }

      const deviceId =
        cloud?.deviceId || identity?.hostname || identity?.deviceId || null;
      const id = deviceId || identity?.hostname || dev?.name || dev?.id;
      if (!id) throw new Error("BLE device did not provide an id/hostname");

      const record = {
        id: String(id),
        name:
          identity?.deviceName || identity?.model || dev?.name || String(id),
        hostname: identity?.hostname || null,
        deviceId: deviceId || String(id),
        authToken: cloud?.token || identity?.token || null,
        cloudBaseUrl: cloud?.baseUrl || "https://tnl.espwifi.io",
        lastSeenAtMs: Date.now(),
      };

      onPaired?.(record, { identity, cloud });
      onClose?.();
    } catch (e) {
      try {
        dev?.gatt?.disconnect?.();
      } catch {
        // ignore
      }
      setError(e?.message || String(e));
    } finally {
      setBusy(false);
      setPhase("idle");
      setStatusText("");
    }
  };

  return (
    <Dialog
      open={open}
      onClose={busy ? undefined : onClose}
      fullWidth
      maxWidth="sm"
    >
      <DialogTitle sx={{ fontWeight: 800 }}>Pair device (BLE)</DialogTitle>
      <DialogContent dividers>
        {!supported && (
          <Alert severity="warning">
            Web Bluetooth isn’t available in this browser. Use Chrome on Android
            or Desktop Chrome for BLE pairing, or we can add a manual pairing
            fallback (token/QR).
          </Alert>
        )}
        {supported && (
          <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
            <Typography variant="body2" sx={{ opacity: 0.9 }}>
              This will open the browser’s BLE chooser. Select your ESPWiFi
              device to fetch its identity + cloud tunnel connection info.
            </Typography>
            {phase === "wifi" && (
              <Box
                sx={{
                  mt: 1.5,
                  display: "flex",
                  flexDirection: "column",
                  gap: 1.5,
                }}
              >
                <Alert severity="info">
                  Device isn’t on the internet yet. Enter WiFi client
                  credentials to connect, then we’ll enable the cloud tunnel.
                </Alert>
                <TextField
                  label="WiFi SSID"
                  value={ssid}
                  onChange={(e) => setSsid(e.target.value)}
                  autoComplete="off"
                  fullWidth
                />
                <TextField
                  label="WiFi Password"
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                  type="password"
                  autoComplete="off"
                  fullWidth
                />
              </Box>
            )}
            {busy && statusText && (
              <Box
                sx={{ mt: 2, display: "flex", alignItems: "center", gap: 1 }}
              >
                <CircularProgress size={18} />
                <Typography variant="body2" sx={{ opacity: 0.9 }}>
                  {statusText}
                </Typography>
              </Box>
            )}
          </Box>
        )}
        {error && (
          <Alert severity="error" sx={{ mt: 2 }}>
            {error}
          </Alert>
        )}
      </DialogContent>
      <DialogActions sx={{ px: 2, py: 1.5, gap: 1 }}>
        <Button onClick={onClose} color="inherit" disabled={busy}>
          Cancel
        </Button>
        {phase === "wifi" ? (
          <Button
            onClick={handleProvisionWifi}
            variant="contained"
            disabled={!supported || busy}
          >
            {busy ? "Provisioning…" : "Connect WiFi + enable tunnel"}
          </Button>
        ) : (
          <Button
            onClick={handlePair}
            variant="contained"
            disabled={!supported || busy}
          >
            {busy ? "Pairing…" : "Start BLE scan"}
          </Button>
        )}
      </DialogActions>
    </Dialog>
  );
}
