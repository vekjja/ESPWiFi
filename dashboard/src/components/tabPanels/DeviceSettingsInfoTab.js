/**
 * @file DeviceSettingsInfoTab.js
 * @brief Comprehensive device information and configuration tab component
 *
 * This component displays and allows editing of all device settings:
 * - Device name and network configuration
 * - WiFi settings (mode, client/AP credentials, power)
 * - Authentication settings
 * - Raw JSON configuration editor
 * - Hardware information (chip, memory, storage)
 *
 * Follows ESP-IDF conventions and industry best practices.
 *
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html
 */

import React, { useState, useEffect } from "react";
import {
  Box,
  Alert,
  Typography,
  Card,
  CardContent,
  Grid,
  Chip,
  Skeleton,
  Tooltip,
  TextField,
  IconButton,
  InputAdornment,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  Collapse,
  Button,
  Switch,
  FormControlLabel,
  LinearProgress,
} from "@mui/material";
import DeveloperBoardIcon from "@mui/icons-material/DeveloperBoard";
import WifiIcon from "@mui/icons-material/Wifi";
import StorageIcon from "@mui/icons-material/Storage";
import SdCardIcon from "@mui/icons-material/SdCard";
import MemoryIcon from "@mui/icons-material/Memory";
import BoltIcon from "@mui/icons-material/Bolt";
import CheckCircleIcon from "@mui/icons-material/CheckCircle";
import EditIcon from "@mui/icons-material/Edit";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import RouterIcon from "@mui/icons-material/Router";
import KeyIcon from "@mui/icons-material/Key";
import CodeIcon from "@mui/icons-material/Code";
import { bytesToHumanReadable, formatUptime } from "../../utils/formatUtils";
import { getRSSIChipColor, isValidRssi } from "../../utils/rssiUtils";

/**
 * DeviceSettingsInfoTab Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.deviceInfo - Device information from /api/info endpoint
 * @param {Object} props.config - Device configuration object
 * @param {Function} props.saveConfigToDevice - Function to save configuration changes
 * @param {boolean} props.infoLoading - Loading state for device info
 * @param {string} props.infoError - Error message if info fetch failed
 * @param {string} props.mode - Current WiFi mode ('client', 'accessPoint', 'apsta')
 * @returns {JSX.Element} The rendered Info tab component
 */
export default function DeviceSettingsInfoTab({
  deviceInfo,
  config,
  saveConfigToDevice,
  infoLoading,
  infoError,
  mode,
}) {
  // ====== Editable State Management ======

  /** @type {[boolean, Function]} Edit mode toggle for device name */
  const [editingDeviceName, setEditingDeviceName] = useState(false);
  /** @type {[string, Function]} Temporary device name during edit */
  const [tempDeviceName, setTempDeviceName] = useState("");

  /** @type {[boolean, Function]} Edit mode toggle for WiFi configuration */
  const [editingWifi, setEditingWifi] = useState(false);
  /** @type {[string, Function]} Temporary WiFi mode during edit */
  const [tempWifiMode, setTempWifiMode] = useState("client");
  /** @type {[string, Function]} Temporary client SSID during edit */
  const [tempClientSsid, setTempClientSsid] = useState("");
  /** @type {[string, Function]} Temporary client password during edit */
  const [tempClientPassword, setTempClientPassword] = useState("");
  /** @type {[string, Function]} Temporary AP SSID during edit */
  const [tempApSsid, setTempApSsid] = useState("");
  /** @type {[string, Function]} Temporary AP password during edit */
  const [tempApPassword, setTempApPassword] = useState("");
  /** @type {[boolean, Function]} Toggle for showing client password */
  const [showClientPassword, setShowClientPassword] = useState(false);
  /** @type {[boolean, Function]} Toggle for showing AP password */
  const [showApPassword, setShowApPassword] = useState(false);

  /** @type {[boolean, Function]} Edit mode toggle for WiFi power settings */
  const [editingPower, setEditingPower] = useState(false);
  /** @type {[string, Function]} Temporary TX power during edit */
  const [tempTxPower, setTempTxPower] = useState("");
  /** @type {[string, Function]} Temporary power save mode during edit */
  const [tempPowerSave, setTempPowerSave] = useState("none");

  /** @type {[boolean, Function]} Edit mode toggle for authentication */
  const [editingAuth, setEditingAuth] = useState(false);
  /** @type {[boolean, Function]} Temporary auth enabled state */
  const [tempAuthEnabled, setTempAuthEnabled] = useState(false);
  /** @type {[string, Function]} Temporary auth username */
  const [tempAuthUsername, setTempAuthUsername] = useState("");
  /** @type {[string, Function]} Temporary auth password */
  const [tempAuthPassword, setTempAuthPassword] = useState("");
  /** @type {[boolean, Function]} Toggle for showing auth password */
  const [showAuthPassword, setShowAuthPassword] = useState(false);

  /** @type {[boolean, Function]} Edit mode toggle for JSON config */
  const [editingJson, setEditingJson] = useState(false);
  /** @type {[string, Function]} Temporary JSON config during edit */
  const [tempJsonConfig, setTempJsonConfig] = useState("");
  /** @type {[string, Function]} JSON validation error message */
  const [jsonError, setJsonError] = useState("");

  /**
   * Initialize temporary values when entering edit mode or when config changes
   * Ensures edit fields are populated with current values
   */
  useEffect(() => {
    if (config) {
      setTempDeviceName(config.deviceName || "");
      setTempWifiMode(config.wifi?.mode || "client");
      setTempClientSsid(config.wifi?.client?.ssid || "");
      setTempClientPassword(config.wifi?.client?.password || "");
      setTempApSsid(config.wifi?.accessPoint?.ssid || "");
      setTempApPassword(config.wifi?.accessPoint?.password || "");
      setTempTxPower(config.wifi?.power?.txPower?.toString() || "19.5");
      setTempPowerSave(config.wifi?.power?.powerSave || "none");
      setTempAuthEnabled(config.auth?.enabled || false);
      setTempAuthUsername(config.auth?.username || "");
      setTempAuthPassword(config.auth?.password || "");
      setTempJsonConfig(JSON.stringify(config, null, 2));
    }
  }, [config]);

  // ====== Event Handlers ======

  /**
   * Handles saving device name changes
   */
  const handleSaveDeviceName = () => {
    if (tempDeviceName && tempDeviceName.trim() !== "") {
      saveConfigToDevice({ deviceName: tempDeviceName.trim() });
      setEditingDeviceName(false);
    }
  };

  /**
   * Handles saving WiFi configuration changes
   * @see ESP-IDF WiFi Configuration
   */
  const handleSaveWifi = () => {
    const wifiConfig = {
      wifi: {
        ...config.wifi,
        mode: tempWifiMode,
        client: {
          ssid: tempClientSsid,
          password: tempClientPassword,
        },
        accessPoint: {
          ssid: tempApSsid || `ESP-${config.deviceName || "WiFi"}`,
          password: tempApPassword,
        },
      },
    };
    saveConfigToDevice(wifiConfig);
    setEditingWifi(false);
  };

  /**
   * Handles saving WiFi power configuration changes
   * TX power range: 8.0 to 20.0 dBm (ESP32 hardware limits)
   */
  const handleSavePower = () => {
    const txPowerValue = parseFloat(tempTxPower);
    if (txPowerValue >= 8.0 && txPowerValue <= 20.0) {
      const powerConfig = {
        wifi: {
          ...config.wifi,
          power: {
            txPower: txPowerValue,
            powerSave: tempPowerSave,
          },
        },
      };
      saveConfigToDevice(powerConfig);
      setEditingPower(false);
    }
  };

  /**
   * Handles saving authentication configuration changes
   */
  const handleSaveAuth = () => {
    const authConfig = {
      auth: {
        ...config.auth,
        enabled: tempAuthEnabled,
        username: tempAuthUsername,
        password: tempAuthPassword,
      },
    };
    saveConfigToDevice(authConfig);
    setEditingAuth(false);
  };

  /**
   * Handles saving JSON configuration changes
   * Validates JSON before saving
   */
  const handleSaveJson = () => {
    try {
      const parsedConfig = JSON.parse(tempJsonConfig);
      saveConfigToDevice(parsedConfig);
      setJsonError("");
      setEditingJson(false);
    } catch (error) {
      setJsonError(`Invalid JSON: ${error.message}`);
    }
  };

  /**
   * Handles JSON textarea changes with validation
   */
  const handleJsonChange = (event) => {
    setTempJsonConfig(event.target.value);
    if (jsonError) {
      setJsonError("");
    }
  };

  // Cancel handlers
  const handleCancelDeviceName = () => {
    setTempDeviceName(config?.deviceName || "");
    setEditingDeviceName(false);
  };

  const handleCancelWifi = () => {
    setTempWifiMode(config?.wifi?.mode || "client");
    setTempClientSsid(config?.wifi?.client?.ssid || "");
    setTempClientPassword(config?.wifi?.client?.password || "");
    setTempApSsid(config?.wifi?.ap?.ssid || "");
    setTempApPassword(config?.wifi?.ap?.password || "");
    setEditingWifi(false);
  };

  const handleCancelPower = () => {
    setTempTxPower(config?.wifi?.power?.txPower?.toString() || "19.5");
    setTempPowerSave(config?.wifi?.power?.powerSave || "none");
    setEditingPower(false);
  };

  const handleCancelAuth = () => {
    setTempAuthEnabled(config?.auth?.enabled || false);
    setTempAuthUsername(config?.auth?.username || "");
    setTempAuthPassword(config?.auth?.password || "");
    setEditingAuth(false);
  };

  const handleCancelJson = () => {
    setTempJsonConfig(JSON.stringify(config, null, 2));
    setJsonError("");
    setEditingJson(false);
  };

  // ====== Render Loading State ======

  if (infoLoading) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Box sx={{ textAlign: "center" }}>
          <Skeleton
            variant="circular"
            width={40}
            height={40}
            sx={{ mx: "auto", mb: 2 }}
          />
          <Skeleton variant="text" width={200} height={24} sx={{ mb: 1 }} />
          <Skeleton variant="text" width={150} height={20} />
        </Box>
      </Box>
    );
  }

  // ====== Render Error State ======

  if (infoError) {
    return (
      <Alert severity="error" sx={{ marginBottom: 2 }}>
        {infoError}
      </Alert>
    );
  }

  // ====== Render No Data State ======

  if (!deviceInfo) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Typography color="text.secondary">
          No device information available
        </Typography>
      </Box>
    );
  }

  // ====== Main Render ======

  // Show loading state if config is not yet available
  if (!config) {
    return (
      <Box
        sx={{
          display: "flex",
          justifyContent: "center",
          alignItems: "center",
          minHeight: "200px",
        }}
      >
        <Typography variant="body1" color="text.secondary">
          Loading configuration...
        </Typography>
      </Box>
    );
  }

  return (
    <Grid container spacing={2} sx={{ pb: 4 }}>
      {/* ==================== WiFi Configuration Card ==================== */}
      <Grid item xs={12}>
        <Card>
          <CardContent>
            <Box
              sx={{
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
                mb: 2,
              }}
            >
              <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                <WifiIcon color="primary" />
                <Typography variant="h6">WiFi Configuration</Typography>
              </Box>
              {!editingWifi && (
                <Tooltip title="Edit WiFi settings">
                  <IconButton onClick={() => setEditingWifi(true)} size="small">
                    <EditIcon />
                  </IconButton>
                </Tooltip>
              )}
            </Box>

            <Collapse in={!editingWifi}>
              <Box sx={{ display: "flex", flexDirection: "column", gap: 1.5 }}>
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    Mode:
                  </Typography>
                  <Chip
                    label={
                      config?.wifi?.mode === "client"
                        ? "Client (Station)"
                        : config?.wifi?.mode === "accessPoint"
                        ? "Access Point"
                        : config?.wifi?.mode === "apsta"
                        ? "AP+STA (Dual Mode)"
                        : "Unknown"
                    }
                    color="primary"
                    size="small"
                  />
                </Box>

                {(config?.wifi?.mode === "client" ||
                  config?.wifi?.mode === "apsta") && (
                  <>
                    <Box
                      sx={{ display: "flex", justifyContent: "space-between" }}
                    >
                      <Typography variant="body2" color="text.secondary">
                        Client SSID:
                      </Typography>
                      <Typography variant="body1" sx={{ fontWeight: 500 }}>
                        {config?.wifi?.client?.ssid || "Not configured"}
                      </Typography>
                    </Box>
                    {deviceInfo.client_ssid && (
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                        }}
                      >
                        <Typography variant="body2" color="text.secondary">
                          Connected Network:
                        </Typography>
                        <Typography variant="body1" sx={{ fontWeight: 500 }}>
                          {deviceInfo.client_ssid}
                        </Typography>
                      </Box>
                    )}
                    {isValidRssi(deviceInfo.rssi) && (
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                        }}
                      >
                        <Typography variant="body2" color="text.secondary">
                          Signal Strength:
                        </Typography>
                        <Chip
                          label={`${deviceInfo.rssi} dBm`}
                          color={getRSSIChipColor(deviceInfo.rssi)}
                          sx={{ fontWeight: 600 }}
                          size="small"
                        />
                      </Box>
                    )}
                  </>
                )}

                {(config?.wifi?.mode === "accessPoint" ||
                  config?.wifi?.mode === "apsta") && (
                  <Box
                    sx={{ display: "flex", justifyContent: "space-between" }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      AP SSID:
                    </Typography>
                    <Typography variant="body1" sx={{ fontWeight: 500 }}>
                      {deviceInfo?.ap_ssid ||
                        (config?.wifi?.accessPoint?.ssid
                          ? `${config.wifi.accessPoint.ssid}-${
                              config.deviceName || "espwifi"
                            }`
                          : "Not configured")}
                    </Typography>
                  </Box>
                )}
              </Box>
            </Collapse>

            {/* WiFi Edit Mode */}
            <Collapse in={editingWifi}>
              <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
                <FormControl fullWidth size="small">
                  <InputLabel>WiFi Mode</InputLabel>
                  <Select
                    value={tempWifiMode}
                    label="WiFi Mode"
                    onChange={(e) => setTempWifiMode(e.target.value)}
                  >
                    <MenuItem value="client">
                      <Box
                        sx={{ display: "flex", alignItems: "center", gap: 1 }}
                      >
                        <WifiIcon fontSize="small" />
                        Client (Station) - Connect to existing network
                      </Box>
                    </MenuItem>
                    <MenuItem value="accessPoint">
                      <Box
                        sx={{ display: "flex", alignItems: "center", gap: 1 }}
                      >
                        <RouterIcon fontSize="small" />
                        Access Point - Create WiFi network
                      </Box>
                    </MenuItem>
                    <MenuItem value="apsta">
                      <Box
                        sx={{ display: "flex", alignItems: "center", gap: 1 }}
                      >
                        <WifiIcon fontSize="small" />
                        <RouterIcon fontSize="small" />
                        AP+STA - Both modes simultaneously
                      </Box>
                    </MenuItem>
                  </Select>
                </FormControl>

                {(tempWifiMode === "client" || tempWifiMode === "apsta") && (
                  <Box
                    sx={{
                      display: "flex",
                      flexDirection: "column",
                      gap: 2,
                      p: 2,
                      backgroundColor: "action.hover",
                      borderRadius: 1,
                    }}
                  >
                    <Typography variant="subtitle2" color="primary">
                      Client Mode Settings
                    </Typography>
                    <TextField
                      fullWidth
                      size="small"
                      label="Network SSID"
                      value={tempClientSsid}
                      onChange={(e) => setTempClientSsid(e.target.value)}
                      placeholder="Network name to connect to"
                      helperText="SSID of the existing WiFi network"
                    />
                    <TextField
                      fullWidth
                      size="small"
                      label="Network Password"
                      type={showClientPassword ? "text" : "password"}
                      value={tempClientPassword}
                      onChange={(e) => setTempClientPassword(e.target.value)}
                      placeholder="Network password"
                      helperText="Leave blank for open networks"
                      InputProps={{
                        endAdornment: (
                          <InputAdornment position="end">
                            <IconButton
                              onClick={() =>
                                setShowClientPassword(!showClientPassword)
                              }
                              edge="end"
                              size="small"
                            >
                              {showClientPassword ? (
                                <VisibilityOffIcon />
                              ) : (
                                <VisibilityIcon />
                              )}
                            </IconButton>
                          </InputAdornment>
                        ),
                      }}
                    />
                  </Box>
                )}

                {(tempWifiMode === "accessPoint" ||
                  tempWifiMode === "apsta") && (
                  <Box
                    sx={{
                      display: "flex",
                      flexDirection: "column",
                      gap: 2,
                      p: 2,
                      backgroundColor: "action.hover",
                      borderRadius: 1,
                    }}
                  >
                    <Typography variant="subtitle2" color="primary">
                      Access Point Settings
                    </Typography>
                    <TextField
                      fullWidth
                      size="small"
                      label="AP SSID"
                      value={tempApSsid}
                      onChange={(e) => setTempApSsid(e.target.value)}
                      placeholder={`ESP-${config?.deviceName || "WiFi"}`}
                      helperText="Name of the WiFi network to create"
                    />
                    <TextField
                      fullWidth
                      size="small"
                      label="AP Password"
                      type={showApPassword ? "text" : "password"}
                      value={tempApPassword}
                      onChange={(e) => setTempApPassword(e.target.value)}
                      placeholder="Minimum 8 characters"
                      helperText="WPA2 password (8-63 characters, leave blank for open AP)"
                      InputProps={{
                        endAdornment: (
                          <InputAdornment position="end">
                            <IconButton
                              onClick={() => setShowApPassword(!showApPassword)}
                              edge="end"
                              size="small"
                            >
                              {showApPassword ? (
                                <VisibilityOffIcon />
                              ) : (
                                <VisibilityIcon />
                              )}
                            </IconButton>
                          </InputAdornment>
                        ),
                      }}
                    />
                  </Box>
                )}

                <Box
                  sx={{ display: "flex", gap: 1, justifyContent: "flex-end" }}
                >
                  <Button
                    variant="outlined"
                    size="small"
                    startIcon={<CancelIcon />}
                    onClick={handleCancelWifi}
                  >
                    Cancel
                  </Button>
                  <Button
                    variant="contained"
                    size="small"
                    startIcon={<SaveIcon />}
                    onClick={handleSaveWifi}
                  >
                    Save WiFi Config
                  </Button>
                </Box>
              </Box>
            </Collapse>
          </CardContent>
        </Card>
      </Grid>

      {/* ==================== WiFi Power Settings Card ==================== */}
      {deviceInfo.wifi_power && (
        <Grid item xs={12} md={6}>
          <Card sx={{ height: "100%", minHeight: 200 }}>
            <CardContent>
              <Box
                sx={{
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  mb: 2,
                }}
              >
                <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                  <BoltIcon color="primary" />
                  <Typography variant="h6">WiFi Power</Typography>
                </Box>
                {!editingPower && (
                  <Tooltip title="Edit power settings">
                    <IconButton
                      onClick={() => setEditingPower(true)}
                      size="small"
                    >
                      <EditIcon />
                    </IconButton>
                  </Tooltip>
                )}
              </Box>

              <Collapse in={!editingPower}>
                <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
                  <Box
                    sx={{ display: "flex", justifyContent: "space-between" }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      TX Power:
                    </Typography>
                    <Typography
                      variant="body1"
                      sx={{ fontWeight: 500, fontFamily: "monospace" }}
                    >
                      {deviceInfo.wifi_power.configured?.txPower?.toFixed(1) ||
                        "N/A"}{" "}
                      dBm
                    </Typography>
                  </Box>

                  {deviceInfo.wifi_power.actual?.txPower && (
                    <Box
                      sx={{ display: "flex", justifyContent: "space-between" }}
                    >
                      <Typography variant="body2" color="text.secondary">
                        Actual TX Power:
                      </Typography>
                      <Typography
                        variant="body1"
                        sx={{ fontWeight: 500, fontFamily: "monospace" }}
                      >
                        {deviceInfo.wifi_power.actual.txPower.toFixed(1)} dBm
                      </Typography>
                    </Box>
                  )}

                  <Box
                    sx={{ display: "flex", justifyContent: "space-between" }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Power Save:
                    </Typography>
                    <Chip
                      label={
                        deviceInfo.wifi_power.configured?.powerSave || "none"
                      }
                      size="small"
                      color={
                        deviceInfo.wifi_power.configured?.powerSave === "none"
                          ? "default"
                          : "success"
                      }
                    />
                  </Box>
                </Box>
              </Collapse>

              <Collapse in={editingPower}>
                <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
                  <TextField
                    fullWidth
                    size="small"
                    type="number"
                    label="TX Power (dBm)"
                    value={tempTxPower}
                    onChange={(e) => setTempTxPower(e.target.value)}
                    inputProps={{ min: 8, max: 20, step: 0.5 }}
                    helperText="Range: 8.0 to 20.0 dBm"
                  />
                  <FormControl fullWidth size="small">
                    <InputLabel>Power Save Mode</InputLabel>
                    <Select
                      value={tempPowerSave}
                      label="Power Save Mode"
                      onChange={(e) => setTempPowerSave(e.target.value)}
                    >
                      <MenuItem value="none">
                        None - Maximum performance
                      </MenuItem>
                      <MenuItem value="min">
                        Minimum - Saves most power
                      </MenuItem>
                      <MenuItem value="max">
                        Maximum - Balance power/performance
                      </MenuItem>
                    </Select>
                  </FormControl>

                  <Box
                    sx={{ display: "flex", gap: 1, justifyContent: "flex-end" }}
                  >
                    <Button
                      variant="outlined"
                      size="small"
                      startIcon={<CancelIcon />}
                      onClick={handleCancelPower}
                    >
                      Cancel
                    </Button>
                    <Button
                      variant="contained"
                      size="small"
                      startIcon={<SaveIcon />}
                      onClick={handleSavePower}
                    >
                      Save
                    </Button>
                  </Box>
                </Box>
              </Collapse>
            </CardContent>
          </Card>
        </Grid>
      )}

      {/* ==================== Network Information Card ==================== */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <WifiIcon color="primary" />
              Network
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  IP Address:
                </Typography>
                <Typography
                  variant="h6"
                  sx={{
                    fontWeight: 600,
                    color: "primary.main",
                    fontFamily: "monospace",
                  }}
                >
                  {deviceInfo.ip || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  mDNS Hostname:
                </Typography>
                <Typography variant="body1" sx={{ fontWeight: 500 }}>
                  {deviceInfo.mdns || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  MAC Address:
                </Typography>
                <Typography
                  variant="body1"
                  sx={{
                    fontWeight: 500,
                    fontFamily: "monospace",
                    fontSize: "0.9rem",
                  }}
                >
                  {deviceInfo.mac || "N/A"}
                </Typography>
              </Box>
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* ==================== Authentication Card ==================== */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Box
              sx={{
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
                mb: 2,
              }}
            >
              <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                <KeyIcon color="primary" />
                <Typography variant="h6">Authentication</Typography>
              </Box>
              {!editingAuth && (
                <Tooltip title="Edit authentication">
                  <IconButton onClick={() => setEditingAuth(true)} size="small">
                    <EditIcon />
                  </IconButton>
                </Tooltip>
              )}
            </Box>

            <Collapse in={!editingAuth}>
              <Box sx={{ display: "flex", flexDirection: "column", gap: 1.5 }}>
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    Status:
                  </Typography>
                  <Chip
                    label={config?.auth?.enabled ? "Enabled" : "Disabled"}
                    color={config?.auth?.enabled ? "success" : "default"}
                    size="small"
                    icon={config?.auth?.enabled ? <KeyIcon /> : undefined}
                  />
                </Box>
                {config?.auth?.enabled && (
                  <Box
                    sx={{ display: "flex", justifyContent: "space-between" }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Username:
                    </Typography>
                    <Typography variant="body1" sx={{ fontWeight: 500 }}>
                      {config?.auth?.username || "Not set"}
                    </Typography>
                  </Box>
                )}
              </Box>
            </Collapse>

            <Collapse in={editingAuth}>
              <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
                <FormControlLabel
                  control={
                    <Switch
                      checked={tempAuthEnabled}
                      onChange={(e) => setTempAuthEnabled(e.target.checked)}
                      color="primary"
                    />
                  }
                  label="Enable Authentication"
                />

                {tempAuthEnabled && (
                  <Box
                    sx={{
                      display: "flex",
                      flexDirection: "column",
                      gap: 2,
                      p: 2,
                      backgroundColor: "action.hover",
                      borderRadius: 1,
                    }}
                  >
                    <TextField
                      fullWidth
                      size="small"
                      label="Username"
                      value={tempAuthUsername}
                      onChange={(e) => setTempAuthUsername(e.target.value)}
                      placeholder="admin"
                    />
                    <TextField
                      fullWidth
                      size="small"
                      label="Password"
                      type={showAuthPassword ? "text" : "password"}
                      value={tempAuthPassword}
                      onChange={(e) => setTempAuthPassword(e.target.value)}
                      placeholder="Enter password"
                      InputProps={{
                        endAdornment: (
                          <InputAdornment position="end">
                            <IconButton
                              onClick={() =>
                                setShowAuthPassword(!showAuthPassword)
                              }
                              edge="end"
                              size="small"
                            >
                              {showAuthPassword ? (
                                <VisibilityOffIcon />
                              ) : (
                                <VisibilityIcon />
                              )}
                            </IconButton>
                          </InputAdornment>
                        ),
                      }}
                    />
                  </Box>
                )}

                <Box
                  sx={{ display: "flex", gap: 1, justifyContent: "flex-end" }}
                >
                  <Button
                    variant="outlined"
                    size="small"
                    startIcon={<CancelIcon />}
                    onClick={handleCancelAuth}
                  >
                    Cancel
                  </Button>
                  <Button
                    variant="contained"
                    size="small"
                    startIcon={<SaveIcon />}
                    onClick={handleSaveAuth}
                  >
                    Save Auth
                  </Button>
                </Box>
              </Box>
            </Collapse>
          </CardContent>
        </Card>
      </Grid>

      {/* ==================== Hardware Information Card ==================== */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Box
              sx={{
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
                mb: 2,
              }}
            >
              <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                <DeveloperBoardIcon color="primary" />
                <Typography variant="h6">Hardware</Typography>
              </Box>
              {!editingDeviceName && (
                <Tooltip title="Edit device name">
                  <IconButton
                    onClick={() => setEditingDeviceName(true)}
                    size="small"
                  >
                    <EditIcon />
                  </IconButton>
                </Tooltip>
              )}
            </Box>

            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              {/* Device Name - Editable */}
              <Box
                sx={{
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                }}
              >
                <Typography variant="body2" color="text.secondary">
                  Device Name:
                </Typography>
                {!editingDeviceName ? (
                  <Typography
                    variant="body1"
                    sx={{ fontWeight: 600, color: "primary.main" }}
                  >
                    {config?.deviceName || "ESPWiFi"}
                  </Typography>
                ) : (
                  <Box
                    sx={{
                      display: "flex",
                      gap: 0.5,
                      alignItems: "center",
                      flex: 1,
                      ml: 1,
                    }}
                  >
                    <TextField
                      fullWidth
                      size="small"
                      value={tempDeviceName}
                      onChange={(e) => setTempDeviceName(e.target.value)}
                      placeholder="Device name"
                    />
                    <Tooltip title="Save">
                      <IconButton
                        onClick={handleSaveDeviceName}
                        color="primary"
                        size="small"
                      >
                        <SaveIcon />
                      </IconButton>
                    </Tooltip>
                    <Tooltip title="Cancel">
                      <IconButton onClick={handleCancelDeviceName} size="small">
                        <CancelIcon />
                      </IconButton>
                    </Tooltip>
                  </Box>
                )}
              </Box>

              {deviceInfo.chip && (
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    Chip Model:
                  </Typography>
                  <Typography variant="body1" sx={{ fontWeight: 500 }}>
                    {deviceInfo.chip}
                  </Typography>
                </Box>
              )}
              {deviceInfo.sdk_version && (
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    ESP-IDF Version:
                  </Typography>
                  <Typography variant="body1" sx={{ fontWeight: 500 }}>
                    {deviceInfo.sdk_version}
                  </Typography>
                </Box>
              )}
              {deviceInfo.uptime !== undefined && (
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    Uptime:
                  </Typography>
                  <Typography variant="body1" sx={{ fontWeight: 500 }}>
                    {formatUptime(deviceInfo.uptime)}
                  </Typography>
                </Box>
              )}
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* ==================== Memory Information Card ==================== */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <MemoryIcon color="primary" />
              Memory
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              {deviceInfo.free_heap !== undefined && (
                <>
                  <Box
                    sx={{ display: "flex", justifyContent: "space-between" }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Free Heap:
                    </Typography>
                    <Typography variant="body1" sx={{ fontWeight: 500 }}>
                      {bytesToHumanReadable(deviceInfo.free_heap)}
                    </Typography>
                  </Box>
                  {deviceInfo.total_heap && (
                    <Box
                      sx={{ display: "flex", justifyContent: "space-between" }}
                    >
                      <Typography variant="body2" color="text.secondary">
                        Total Heap:
                      </Typography>
                      <Typography variant="body1" sx={{ fontWeight: 500 }}>
                        {bytesToHumanReadable(deviceInfo.total_heap)}
                      </Typography>
                    </Box>
                  )}
                  {deviceInfo.used_heap && deviceInfo.total_heap && (
                    <>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                        }}
                      >
                        <Typography variant="body2" color="text.secondary">
                          Used Heap:
                        </Typography>
                        <Typography variant="body1" sx={{ fontWeight: 500 }}>
                          {bytesToHumanReadable(deviceInfo.used_heap)}
                        </Typography>
                      </Box>
                      <Box sx={{ mt: 1 }}>
                        <Box
                          sx={{
                            display: "flex",
                            justifyContent: "space-between",
                            mb: 0.5,
                          }}
                        >
                          <Typography variant="caption" color="text.secondary">
                            Memory Usage
                          </Typography>
                          <Typography variant="caption" color="text.secondary">
                            {(
                              (deviceInfo.used_heap / deviceInfo.total_heap) *
                              100
                            ).toFixed(1)}
                            %
                          </Typography>
                        </Box>
                        <LinearProgress
                          variant="determinate"
                          value={
                            (deviceInfo.used_heap / deviceInfo.total_heap) * 100
                          }
                          sx={{
                            height: 8,
                            borderRadius: 1,
                            backgroundColor: "action.hover",
                            "& .MuiLinearProgress-bar": {
                              borderRadius: 1,
                              backgroundColor:
                                (deviceInfo.used_heap / deviceInfo.total_heap) *
                                  100 >
                                80
                                  ? "error.main"
                                  : (deviceInfo.used_heap /
                                      deviceInfo.total_heap) *
                                      100 >
                                    60
                                  ? "warning.main"
                                  : "success.main",
                            },
                          }}
                        />
                      </Box>
                    </>
                  )}
                </>
              )}
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* ==================== Storage Information Card ==================== */}
      <Grid item xs={12}>
        <Card>
          <CardContent sx={{ pb: 3 }}>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <StorageIcon color="primary" />
              Storage
            </Typography>
            <Grid container spacing={2} sx={{ mt: 0.5 }}>
              {/* LittleFS */}
              {deviceInfo.littlefs_total > 0 && (
                <Grid item xs={12} md={6}>
                  <Box
                    sx={{
                      p: 2,
                      backgroundColor: "action.hover",
                      borderRadius: 1,
                      height: "100%",
                    }}
                  >
                    <Box
                      sx={{
                        display: "flex",
                        alignItems: "center",
                        gap: 1,
                        mb: 1,
                      }}
                    >
                      <StorageIcon fontSize="small" color="primary" />
                      <Typography variant="subtitle1" fontWeight={600}>
                        LittleFS
                      </Typography>
                      <Chip
                        icon={<CheckCircleIcon />}
                        label="Mounted"
                        size="small"
                        color="success"
                      />
                    </Box>
                    <Box
                      sx={{ display: "flex", flexDirection: "column", gap: 1 }}
                    >
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                        }}
                      >
                        <Typography variant="body2" color="text.secondary">
                          Used:
                        </Typography>
                        <Typography variant="body2" sx={{ fontWeight: 500 }}>
                          {bytesToHumanReadable(deviceInfo.littlefs_used)}
                        </Typography>
                      </Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                        }}
                      >
                        <Typography variant="body2" color="text.secondary">
                          Total:
                        </Typography>
                        <Typography variant="body2" sx={{ fontWeight: 500 }}>
                          {bytesToHumanReadable(deviceInfo.littlefs_total)}
                        </Typography>
                      </Box>
                      <Box sx={{ mt: 0.5 }}>
                        <Box
                          sx={{
                            display: "flex",
                            justifyContent: "space-between",
                            mb: 0.5,
                          }}
                        >
                          <Typography variant="caption" color="text.secondary">
                            Storage Usage
                          </Typography>
                          <Typography variant="caption" color="text.secondary">
                            {(
                              (deviceInfo.littlefs_used /
                                deviceInfo.littlefs_total) *
                              100
                            ).toFixed(1)}
                            %
                          </Typography>
                        </Box>
                        <LinearProgress
                          variant="determinate"
                          value={
                            (deviceInfo.littlefs_used /
                              deviceInfo.littlefs_total) *
                            100
                          }
                          sx={{
                            height: 8,
                            borderRadius: 1,
                            backgroundColor: "action.hover",
                            "& .MuiLinearProgress-bar": {
                              borderRadius: 1,
                              backgroundColor:
                                (deviceInfo.littlefs_used /
                                  deviceInfo.littlefs_total) *
                                  100 >
                                80
                                  ? "error.main"
                                  : (deviceInfo.littlefs_used /
                                      deviceInfo.littlefs_total) *
                                      100 >
                                    60
                                  ? "warning.main"
                                  : "success.main",
                            },
                          }}
                        />
                      </Box>
                    </Box>
                  </Box>
                </Grid>
              )}

              {/* SD Card */}
              {deviceInfo.sd_total > 0 && (
                <Grid item xs={12} md={6}>
                  <Box
                    sx={{
                      p: 2,
                      backgroundColor: "action.hover",
                      borderRadius: 1,
                      height: "100%",
                    }}
                  >
                    <Box
                      sx={{
                        display: "flex",
                        alignItems: "center",
                        gap: 1,
                        mb: 1,
                      }}
                    >
                      <SdCardIcon fontSize="small" color="primary" />
                      <Typography variant="subtitle1" fontWeight={600}>
                        SD Card
                      </Typography>
                      <Chip
                        icon={<CheckCircleIcon />}
                        label="Mounted"
                        size="small"
                        color="success"
                      />
                    </Box>
                    <Box
                      sx={{
                        display: "flex",
                        flexDirection: "column",
                        gap: 1,
                      }}
                    >
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                        }}
                      >
                        <Typography variant="body2" color="text.secondary">
                          Used:
                        </Typography>
                        <Typography variant="body2" sx={{ fontWeight: 500 }}>
                          {bytesToHumanReadable(deviceInfo.sd_used)}
                        </Typography>
                      </Box>
                      <Box
                        sx={{
                          display: "flex",
                          justifyContent: "space-between",
                        }}
                      >
                        <Typography variant="body2" color="text.secondary">
                          Total:
                        </Typography>
                        <Typography variant="body2" sx={{ fontWeight: 500 }}>
                          {bytesToHumanReadable(deviceInfo.sd_total)}
                        </Typography>
                      </Box>
                      <Box sx={{ mt: 0.5 }}>
                        <Box
                          sx={{
                            display: "flex",
                            justifyContent: "space-between",
                            mb: 0.5,
                          }}
                        >
                          <Typography variant="caption" color="text.secondary">
                            Storage Usage
                          </Typography>
                          <Typography variant="caption" color="text.secondary">
                            {(
                              (deviceInfo.sd_used / deviceInfo.sd_total) *
                              100
                            ).toFixed(1)}
                            %
                          </Typography>
                        </Box>
                        <LinearProgress
                          variant="determinate"
                          value={
                            (deviceInfo.sd_used / deviceInfo.sd_total) * 100
                          }
                          sx={{
                            height: 8,
                            borderRadius: 1,
                            backgroundColor: "action.hover",
                            "& .MuiLinearProgress-bar": {
                              borderRadius: 1,
                              backgroundColor:
                                (deviceInfo.sd_used / deviceInfo.sd_total) *
                                  100 >
                                80
                                  ? "error.main"
                                  : (deviceInfo.sd_used / deviceInfo.sd_total) *
                                      100 >
                                    60
                                  ? "warning.main"
                                  : "success.main",
                            },
                          }}
                        />
                      </Box>
                    </Box>
                  </Box>
                </Grid>
              )}
            </Grid>
          </CardContent>
        </Card>
      </Grid>

      {/* ==================== JSON Configuration Editor ==================== */}
      <Grid item xs={12}>
        <Card>
          <CardContent>
            <Box
              sx={{
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
                mb: 2,
              }}
            >
              <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                <CodeIcon color="primary" />
                <Typography variant="h6">JSON Configuration</Typography>
              </Box>
              {!editingJson && (
                <Tooltip title="Edit JSON configuration">
                  <IconButton onClick={() => setEditingJson(true)} size="small">
                    <EditIcon />
                  </IconButton>
                </Tooltip>
              )}
            </Box>

            {jsonError && (
              <Alert severity="error" sx={{ mb: 2 }}>
                {jsonError}
              </Alert>
            )}

            <Box
              sx={{
                border: 1,
                borderColor: jsonError ? "error.main" : "divider",
                borderRadius: 1,
                overflow: "hidden",
              }}
            >
              <Box
                sx={{
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  px: 2,
                  py: 1,
                  bgcolor: "action.hover",
                  borderBottom: 1,
                  borderColor: "divider",
                }}
              >
                <Typography variant="caption" color="text.secondary">
                  Configuration JSON
                </Typography>
                {!editingJson && (
                  <Typography variant="caption" color="text.secondary">
                    Read-only
                  </Typography>
                )}
              </Box>
              <TextField
                value={
                  editingJson
                    ? tempJsonConfig
                    : config
                    ? JSON.stringify(config, null, 2)
                    : "Loading..."
                }
                onChange={handleJsonChange}
                variant="standard"
                fullWidth
                multiline
                error={!!jsonError}
                InputProps={{
                  disableUnderline: true,
                  readOnly: !editingJson,
                }}
                sx={{
                  "& .MuiInputBase-root": {
                    height: "60vh",
                    overflowY: "auto",
                    alignItems: "flex-start",
                    px: 2,
                    py: 1.5,
                  },
                  "& .MuiInputBase-input": {
                    fontFamily: "monospace",
                    fontSize: "0.875rem",
                    lineHeight: 1.6,
                    height: "100% !important",
                    overflow: "auto !important",
                  },
                }}
              />
            </Box>

            {editingJson && (
              <Box
                sx={{
                  display: "flex",
                  gap: 1,
                  justifyContent: "flex-end",
                  mt: 2,
                }}
              >
                <Button
                  variant="outlined"
                  size="small"
                  startIcon={<CancelIcon />}
                  onClick={handleCancelJson}
                >
                  Cancel
                </Button>
                <Button
                  variant="contained"
                  size="small"
                  startIcon={<SaveIcon />}
                  onClick={handleSaveJson}
                >
                  Save JSON
                </Button>
              </Box>
            )}
          </CardContent>
        </Card>
      </Grid>
    </Grid>
  );
}
