/**
 * @file WifiConfigInfoCard.js
 * @brief WiFi configuration card with WiFi settings
 *
 * Displays and allows editing of WiFi mode, client/AP credentials
 */

import React, { useState, useEffect } from "react";
import {
  Box,
  Typography,
  Chip,
  Button,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  InputAdornment,
  IconButton,
  Grid,
  TextField,
} from "@mui/material";
import WifiIcon from "@mui/icons-material/Wifi";
import RouterIcon from "@mui/icons-material/Router";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";
import MaskedValueField from "../common/MaskedValueField";
import { getRSSIChipColor, isValidRssi } from "../../utils/rssiUtils";

/**
 * WifiConfigInfoCard Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.config - Device configuration
 * @param {Object} props.deviceInfo - Device information
 * @param {Function} props.onSave - Callback to save configuration
 * @returns {JSX.Element} The rendered WiFi config card
 */
export default function WifiConfigInfoCard({ config, deviceInfo, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [tempWifiMode, setTempWifiMode] = useState("client");
  const [tempClientSsid, setTempClientSsid] = useState("");
  const [tempClientPassword, setTempClientPassword] = useState("");
  const [tempApPassword, setTempApPassword] = useState("");
  const [showClientPassword, setShowClientPassword] = useState(false);
  const [showApPassword, setShowApPassword] = useState(false);

  useEffect(() => {
    if (config) {
      setTempWifiMode(config.wifi?.mode || "client");
      setTempClientSsid(config.wifi?.client?.ssid || "");
      setTempClientPassword(config.wifi?.client?.password || "");
      setTempApPassword(config.wifi?.accessPoint?.password || "");
    }
  }, [config]);

  const getApSsidDisplay = () =>
    config?.wifi?.accessPoint?.ssid || "Not configured";

  const handleSave = () => {
    const wifiConfig = {
      wifi: {
        ...config.wifi,
        mode: tempWifiMode,
        client: {
          ssid: tempClientSsid,
          password: tempClientPassword,
        },
        accessPoint: {
          // AP SSID is derived from hostname; don't make it user-editable here.
          ...(config?.wifi?.accessPoint || {}),
          password: tempApPassword,
        },
      },
    };
    onSave(wifiConfig);
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempWifiMode(config?.wifi?.mode || "client");
    setTempClientSsid(config?.wifi?.client?.ssid || "");
    setTempClientPassword(config?.wifi?.client?.password || "");
    setTempApPassword(config?.wifi?.accessPoint?.password || "");
    setIsEditing(false);
  };

  const viewContent = (
    <>
      <Grid container spacing={2}>
        {(config?.wifi?.mode === "client" ||
          config?.wifi?.mode === "apsta") && (
          <Grid item xs={12} sm={6} md={3}>
            <InfoRow
              label="Client SSID:"
              value={config?.wifi?.client?.ssid || "Not configured"}
            />
          </Grid>
        )}

        {(config?.wifi?.mode === "client" ||
          config?.wifi?.mode === "apsta") && (
          <Grid item xs={12} sm={6} md={3}>
            <InfoRow
              label="Client Password:"
              value={
                <MaskedValueField
                  value={config?.wifi?.client?.password || "—"}
                  blur={Boolean(config?.wifi?.client?.password)}
                  defaultShow={false}
                />
              }
            />
          </Grid>
        )}

        {(config?.wifi?.mode === "accessPoint" ||
          config?.wifi?.mode === "apsta") && (
          <Grid item xs={12} sm={6} md={3}>
            <InfoRow label="AP SSID:" value={getApSsidDisplay()} />
          </Grid>
        )}

        {(config?.wifi?.mode === "accessPoint" ||
          config?.wifi?.mode === "apsta") && (
          <Grid item xs={12} sm={6} md={3}>
            <InfoRow
              label="AP Password:"
              value={
                <MaskedValueField
                  value={config?.wifi?.accessPoint?.password || "—"}
                  blur={Boolean(config?.wifi?.accessPoint?.password)}
                  defaultShow={false}
                />
              }
            />
          </Grid>
        )}

        {/* Show mode + signal under SSID/password block */}
        <Grid item xs={12} sm={6} md={3}>
          <InfoRow
            label="Mode:"
            value={
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
                sx={{ mt: 0.5 }}
              />
            }
          />
        </Grid>

        {(config?.wifi?.mode === "client" || config?.wifi?.mode === "apsta") &&
          isValidRssi(deviceInfo.rssi) && (
            <Grid item xs={12} sm={6} md={3}>
              <InfoRow
                label="Signal Strength:"
                value={
                  <Chip
                    label={`${deviceInfo.rssi} dBm`}
                    color={getRSSIChipColor(deviceInfo.rssi)}
                    sx={{ fontWeight: 600, mt: 0.5 }}
                    size="small"
                  />
                }
              />
            </Grid>
          )}
      </Grid>
    </>
  );

  const editContent = (
    <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
      <FormControl fullWidth size="small">
        <InputLabel>WiFi Mode</InputLabel>
        <Select
          value={tempWifiMode}
          label="WiFi Mode"
          onChange={(e) => setTempWifiMode(e.target.value)}
        >
          <MenuItem value="client">
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
              <WifiIcon fontSize="small" />
              Client (Station) - Connect to existing network
            </Box>
          </MenuItem>
          <MenuItem value="accessPoint">
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
              <RouterIcon fontSize="small" />
              Access Point - Create WiFi network
            </Box>
          </MenuItem>
          <MenuItem value="apsta">
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
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
                    onClick={() => setShowClientPassword(!showClientPassword)}
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

      {(tempWifiMode === "accessPoint" || tempWifiMode === "apsta") && (
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
            value={getApSsidDisplay()}
            disabled
            helperText="Derived from hostname (not editable)"
          />
          <TextField
            fullWidth
            size="small"
            label="AP Password"
            type={showApPassword ? "text" : "password"}
            value={tempApPassword}
            onChange={(e) => setTempApPassword(e.target.value)}
            placeholder="Access point password"
            helperText="Minimum 8 characters for WPA2"
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

      <Box sx={{ display: "flex", gap: 1, justifyContent: "flex-end" }}>
        <Button
          variant="outlined"
          size="small"
          startIcon={<CancelIcon />}
          onClick={handleCancel}
        >
          Cancel
        </Button>
        <Button
          variant="contained"
          size="small"
          startIcon={<SaveIcon />}
          onClick={handleSave}
        >
          Save WiFi Config
        </Button>
      </Box>
    </Box>
  );

  return (
    <InfoCard
      title="WiFi Configuration"
      icon={WifiIcon}
      editable
      isEditing={isEditing}
      onEdit={() => setIsEditing(true)}
      gridSize={{ xs: 12 }}
      editContent={editContent}
    >
      {viewContent}
    </InfoCard>
  );
}
