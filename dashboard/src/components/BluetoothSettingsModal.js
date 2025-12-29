import React, {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import {
  Alert,
  Box,
  Button,
  Card,
  CardContent,
  CircularProgress,
  Divider,
  Stack,
  TextField,
  Typography,
} from "@mui/material";
import {
  Bluetooth as BluetoothIcon,
  BluetoothConnected as BluetoothConnectedIcon,
  BluetoothDisabled as BluetoothDisabledIcon,
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
}) {
  const initializedRef = useRef(false);

  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [btStatus, setBtStatus] = useState(null);
  const [scanResults, setScanResults] = useState([]);
  const [scanning, setScanning] = useState(false);

  const initialEnabled = config?.bluetooth?.enabled || false;
  const initialAudioEnabled = config?.bluetooth?.audio?.enabled || false;
  const initialPairingSeconds = config?.bluetooth?.audio?.pairingSeconds ?? 10;
  const initialTargetName = config?.bluetooth?.audio?.targetName || "";
  const initialTrackPath =
    config?.bluetooth?.audio?.defaultTrack || "/sd/music.wav";

  const [enabled, setEnabled] = useState(initialEnabled);
  const [audioEnabled, setAudioEnabled] = useState(initialAudioEnabled);
  const [pairingSeconds, setPairingSeconds] = useState(
    Number.isFinite(initialPairingSeconds) ? initialPairingSeconds : 10
  );
  const [targetName, setTargetName] = useState(initialTargetName);
  const [trackPath, setTrackPath] = useState(initialTrackPath);

  const isDisabled = !deviceOnline || !config;

  const fetchStatus = useCallback(async () => {
    if (!deviceOnline || !enabled) {
      setBtStatus(null);
      return;
    }
    try {
      const response = await fetch(
        buildApiUrl("/api/bluetooth/status"),
        getFetchOptions()
      );
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const data = await response.json();
      setBtStatus(data);
    } catch {
      // Status endpoint may be unavailable early in boot; keep UI usable.
      setBtStatus(null);
    }
  }, [deviceOnline, enabled]);

  const postJson = useCallback(async (path, body = null) => {
    const response = await fetch(
      buildApiUrl(path),
      getFetchOptions({
        method: "POST",
        body: body ? JSON.stringify(body) : undefined,
      })
    );
    if (!response.ok) {
      const text = await response.text();
      throw new Error(`${response.status}: ${text || response.statusText}`);
    }
    return response;
  }, []);

  useEffect(() => {
    if (open && !initializedRef.current) {
      setEnabled(initialEnabled);
      setAudioEnabled(initialAudioEnabled);
      setPairingSeconds(
        Number.isFinite(initialPairingSeconds) ? initialPairingSeconds : 10
      );
      setTargetName(initialTargetName);
      setTrackPath(initialTrackPath);
      setError(null);
      setScanResults([]);
      initializedRef.current = true;
      fetchStatus();
    } else if (!open) {
      initializedRef.current = false;
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  useEffect(() => {
    if (!open) return undefined;
    const interval = setInterval(fetchStatus, 2000);
    return () => clearInterval(interval);
  }, [open, fetchStatus]);

  const statusLabel = useMemo(() => {
    if (!enabled) return "Disabled";
    if (btStatus?.connected) return "Connected";
    if (btStatus?.connecting) return "Connecting";
    return "Ready";
  }, [enabled, btStatus]);

  const title = useMemo(() => {
    if (!enabled) return "Bluetooth (Disabled)";
    if (btStatus?.connected) return "Bluetooth (Connected)";
    if (btStatus?.connecting) return "Bluetooth (Connecting)";
    return "Bluetooth";
  }, [enabled, btStatus]);

  const TitleIcon = useMemo(() => {
    if (!enabled) return BluetoothDisabledIcon;
    if (btStatus?.connected) return BluetoothConnectedIcon;
    return BluetoothIcon;
  }, [enabled, btStatus]);

  const handleSaveConfig = async () => {
    if (loading || isDisabled) return;
    setLoading(true);
    setError(null);
    try {
      const configToSave = {
        ...config,
        bluetooth: {
          ...(config.bluetooth || {}),
          enabled,
          audio: {
            ...(config.bluetooth?.audio || {}),
            enabled: audioEnabled,
            pairingSeconds: Math.max(
              1,
              Math.min(60, Number(pairingSeconds) || 10)
            ),
            targetName,
            defaultTrack: trackPath,
          },
        },
      };
      saveConfig?.(configToSave);
      await Promise.resolve(saveConfigToDevice?.(configToSave));
    } catch (e) {
      setError(e.message || "Failed to save config");
    } finally {
      setLoading(false);
    }
  };

  const handleScan = async () => {
    if (loading || isDisabled || !enabled) return;
    setLoading(true);
    setError(null);
    try {
      setScanning(true);
      setScanResults([]);
      const seconds = Math.max(1, Math.min(60, Number(pairingSeconds) || 10));

      // Start a short discovery window.
      await postJson(`/api/bluetooth/pairing/start?seconds=${seconds}`);

      const startedAt = Date.now();
      while (Date.now() - startedAt < seconds * 1000) {
        // eslint-disable-next-line no-await-in-loop
        const response = await fetch(
          buildApiUrl("/api/bluetooth/scan"),
          getFetchOptions()
        );
        if (response.ok) {
          // eslint-disable-next-line no-await-in-loop
          const data = await response.json();
          setScanResults(data.devices || []);
          if (data.scanning === false) {
            break;
          }
        }
        // eslint-disable-next-line no-await-in-loop
        await fetchStatus();
        // eslint-disable-next-line no-await-in-loop
        await new Promise((r) => setTimeout(r, 800));
      }
    } catch (e) {
      setScanResults([]);
      setError(e.message || "Scan failed");
    } finally {
      setScanning(false);
      setLoading(false);
    }
  };

  const handlePairingStart = async () => {
    if (loading || isDisabled || !enabled) return;
    setLoading(true);
    setError(null);
    try {
      const seconds = Math.max(1, Math.min(60, Number(pairingSeconds) || 10));
      await postJson(`/api/bluetooth/pairing/start?seconds=${seconds}`);
      await fetchStatus();
    } catch (e) {
      setError(e.message || "Failed to start pairing");
    } finally {
      setLoading(false);
    }
  };

  const handlePairingStop = async () => {
    if (loading || isDisabled || !enabled) return;
    setLoading(true);
    setError(null);
    try {
      await postJson("/api/bluetooth/pairing/stop");
      await fetchStatus();
    } catch (e) {
      setError(e.message || "Failed to stop pairing");
    } finally {
      setLoading(false);
    }
  };

  const handleConnect = async (name = targetName) => {
    if (loading || isDisabled || !enabled) return;
    if (!name) {
      setError("Enter a speaker name to connect");
      return;
    }
    setLoading(true);
    setError(null);
    try {
      await postJson("/api/bluetooth/connect", { name });
      await fetchStatus();
    } catch (e) {
      setError(e.message || "Failed to connect");
    } finally {
      setLoading(false);
    }
  };

  const handleDisconnect = async () => {
    if (loading || isDisabled || !enabled) return;
    setLoading(true);
    setError(null);
    try {
      await postJson("/api/bluetooth/disconnect");
      await fetchStatus();
    } catch (e) {
      setError(e.message || "Failed to disconnect");
    } finally {
      setLoading(false);
    }
  };

  const handlePlay = async () => {
    if (loading || isDisabled || !enabled || !audioEnabled) return;
    if (!trackPath) {
      setError("Enter a WAV path (e.g. /sd/music.wav)");
      return;
    }
    setLoading(true);
    setError(null);
    try {
      await postJson("/api/bluetooth/audio/play", { path: trackPath });
      await fetchStatus();
    } catch (e) {
      setError(e.message || "Failed to start playback");
    } finally {
      setLoading(false);
    }
  };

  const handleStop = async () => {
    if (loading || isDisabled || !enabled || !audioEnabled) return;
    setLoading(true);
    setError(null);
    try {
      await postJson("/api/bluetooth/audio/stop");
      await fetchStatus();
    } catch (e) {
      setError(e.message || "Failed to stop playback");
    } finally {
      setLoading(false);
    }
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <Stack direction="row" spacing={1} sx={{ alignItems: "center" }}>
          <TitleIcon fontSize="small" />
          <span>{title}</span>
        </Stack>
      }
      maxWidth="md"
    >
      <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
        {error && <Alert severity="error">{error}</Alert>}

        <Card variant="outlined">
          <CardContent>
            <Stack spacing={1}>
              <Typography variant="h6">Status</Typography>
              <Typography variant="body2" color="text.secondary">
                {statusLabel}
              </Typography>
              <Typography variant="body2" sx={{ fontFamily: "monospace" }}>
                {btStatus ? JSON.stringify(btStatus) : "No status yet"}
              </Typography>
              <Button
                variant="outlined"
                onClick={fetchStatus}
                disabled={loading || isDisabled || !enabled}
                sx={{ alignSelf: "flex-start" }}
              >
                Refresh
              </Button>
            </Stack>
          </CardContent>
        </Card>

        <Card variant="outlined">
          <CardContent>
            <Stack spacing={2}>
              <Typography variant="h6">Config</Typography>

              <Stack direction="row" spacing={2} sx={{ flexWrap: "wrap" }}>
                <Button
                  variant={enabled ? "contained" : "outlined"}
                  onClick={() => setEnabled((v) => !v)}
                  disabled={loading || isDisabled}
                >
                  {enabled ? "Enabled" : "Disabled"}
                </Button>
                <Button
                  variant={audioEnabled ? "contained" : "outlined"}
                  onClick={() => setAudioEnabled((v) => !v)}
                  disabled={loading || isDisabled || !enabled}
                >
                  {audioEnabled ? "Audio Enabled" : "Audio Disabled"}
                </Button>
                <Button
                  variant="contained"
                  onClick={handleSaveConfig}
                  disabled={loading || isDisabled}
                >
                  Save to Device
                </Button>
              </Stack>

              <TextField
                label="Speaker Name (targetName)"
                value={targetName}
                onChange={(e) => setTargetName(e.target.value)}
                disabled={loading || isDisabled || !enabled}
                fullWidth
              />
              <TextField
                label="WAV Path on SD (defaultTrack)"
                value={trackPath}
                onChange={(e) => setTrackPath(e.target.value)}
                disabled={loading || isDisabled || !enabled}
                fullWidth
                helperText="WAV-first: PCM16 stereo 44.1kHz (e.g. /sd/music.wav)"
              />
              <TextField
                label="Pairing Window (seconds)"
                value={pairingSeconds}
                onChange={(e) => setPairingSeconds(e.target.value)}
                disabled={loading || isDisabled || !enabled}
                type="number"
                inputProps={{ min: 1, max: 60 }}
                sx={{ maxWidth: 260 }}
              />
            </Stack>
          </CardContent>
        </Card>

        <Card variant="outlined">
          <CardContent>
            <Stack spacing={2}>
              <Typography variant="h6">Connect</Typography>
              <Alert severity="info" icon={<InfoIcon />}>
                If scan results are empty, enter the speaker’s Bluetooth name
                and press Connect.
              </Alert>

              <Stack direction="row" spacing={2} sx={{ flexWrap: "wrap" }}>
                <Button
                  variant="outlined"
                  onClick={handleScan}
                  disabled={loading || scanning || isDisabled || !enabled}
                >
                  {scanning ? (
                    <>
                      <CircularProgress size={16} sx={{ mr: 1 }} />
                      Scanning…
                    </>
                  ) : (
                    "Scan"
                  )}
                </Button>
                <Button
                  variant="outlined"
                  onClick={handlePairingStart}
                  disabled={loading || isDisabled || !enabled}
                >
                  Start Pairing
                </Button>
                <Button
                  variant="outlined"
                  onClick={handlePairingStop}
                  disabled={loading || isDisabled || !enabled}
                >
                  Stop Pairing
                </Button>
                <Button
                  variant="contained"
                  onClick={() => handleConnect(targetName)}
                  disabled={loading || isDisabled || !enabled || !targetName}
                >
                  Connect
                </Button>
                <Button
                  variant="outlined"
                  onClick={handleDisconnect}
                  disabled={loading || isDisabled || !enabled}
                >
                  Disconnect
                </Button>
              </Stack>

              {scanResults.length > 0 && (
                <>
                  <Divider />
                  <Stack spacing={1}>
                    <Typography variant="subtitle1">Scan Results</Typography>
                    {[...scanResults]
                      .sort((a, b) => (b?.rssi ?? -999) - (a?.rssi ?? -999))
                      .map((d) => (
                        <Stack
                          key={`${d.addr}-${d.name}-${d.rssi}`}
                          direction="row"
                          spacing={1}
                          sx={{ alignItems: "center", flexWrap: "wrap" }}
                        >
                          <Typography sx={{ fontFamily: "monospace" }}>
                            {d.name || "(no name)"} ({d.addr}) rssi={d.rssi}
                          </Typography>
                          {d.name && (
                            <Button
                              size="small"
                              variant="outlined"
                              onClick={() => {
                                setTargetName(d.name);
                              }}
                              disabled={loading || isDisabled || !enabled}
                            >
                              Select
                            </Button>
                          )}
                        </Stack>
                      ))}
                  </Stack>
                </>
              )}
            </Stack>
          </CardContent>
        </Card>

        <Card variant="outlined">
          <CardContent>
            <Stack spacing={2}>
              <Typography variant="h6">Playback</Typography>
              <Stack direction="row" spacing={2} sx={{ flexWrap: "wrap" }}>
                <Button
                  variant="contained"
                  onClick={handlePlay}
                  disabled={loading || isDisabled || !enabled || !audioEnabled}
                >
                  Play WAV
                </Button>
                <Button
                  variant="outlined"
                  onClick={handleStop}
                  disabled={loading || isDisabled || !enabled || !audioEnabled}
                >
                  Stop
                </Button>
              </Stack>

              {loading && (
                <Stack
                  direction="row"
                  spacing={1}
                  sx={{ alignItems: "center" }}
                >
                  <CircularProgress size={18} />
                  <Typography variant="body2">Working…</Typography>
                </Stack>
              )}
            </Stack>
          </CardContent>
        </Card>
      </Box>
    </SettingsModal>
  );
}
