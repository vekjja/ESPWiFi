/**
 * @file WifiConfigInfoCard.js
 * @brief WiFi configuration card with editable device name and WiFi settings
 *
 * Displays and allows editing of device name, WiFi mode, client/AP credentials
 */

import React, { useState, useEffect } from "react";
import {
  Box,
  Typography,
  Chip,
  TextField,
  Button,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  InputAdornment,
  IconButton,
} from "@mui/material";
import WifiIcon from "@mui/icons-material/Wifi";
import RouterIcon from "@mui/icons-material/Router";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";
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
  const [tempDeviceName, setTempDeviceName] = useState("");
  const [tempWifiMode, setTempWifiMode] = useState("client");
  const [tempClientSsid, setTempClientSsid] = useState("");
  const [tempClientPassword, setTempClientPassword] = useState("");
  const [tempApSsid, setTempApSsid] = useState("");
  const [tempApPassword, setTempApPassword] = useState("");
  const [showClientPassword, setShowClientPassword] = useState(false);
  const [showApPassword, setShowApPassword] = useState(false);

  useEffect(() => {
    if (config) {
      setTempDeviceName(config.deviceName || "");
      setTempWifiMode(config.wifi?.mode || "client");
      setTempClientSsid(config.wifi?.client?.ssid || "");
      setTempClientPassword(config.wifi?.client?.password || "");
      setTempApSsid(config.wifi?.accessPoint?.ssid || "");
      setTempApPassword(config.wifi?.accessPoint?.password || "");
    }
  }, [config]);

  const handleSave = () => {
    const wifiConfig = {
      deviceName: tempDeviceName.trim(),
      wifi: {
        ...config.wifi,
        mode: tempWifiMode,
        client: {
          ssid: tempClientSsid,
          password: tempClientPassword,
        },
        accessPoint: {
          ssid: tempApSsid || `ESP-${tempDeviceName.trim() || "WiFi"}`,
          password: tempApPassword,
        },
      },
    };
    onSave(wifiConfig);
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempDeviceName(config?.deviceName || "");
    setTempWifiMode(config?.wifi?.mode || "client");
    setTempClientSsid(config?.wifi?.client?.ssid || "");
    setTempClientPassword(config?.wifi?.client?.password || "");
    setTempApSsid(config?.wifi?.accessPoint?.ssid || "");
    setTempApPassword(config?.wifi?.accessPoint?.password || "");
    setIsEditing(false);
  };

  const viewContent = (
    <>
      <InfoRow
        label="Device Name:"
        value={config?.deviceName || "ESPWiFi"}
        valueProps={{ fontWeight: 600, color: "primary.main" }}
      />

      {(config?.wifi?.mode === "client" || config?.wifi?.mode === "apsta") && (
        <InfoRow
          label="Client SSID:"
          value={config?.wifi?.client?.ssid || "Not configured"}
        />
      )}

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

      {(config?.wifi?.mode === "client" || config?.wifi?.mode === "apsta") &&
        isValidRssi(deviceInfo.rssi) && (
          <Box sx={{ display: "flex", justifyContent: "space-between" }}>
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

      {(config?.wifi?.mode === "accessPoint" ||
        config?.wifi?.mode === "apsta") && (
        <InfoRow
          label="AP SSID:"
          value={
            deviceInfo?.ap_ssid ||
            (config?.wifi?.accessPoint?.ssid
              ? `${config.wifi.accessPoint.ssid}-${
                  config.deviceName || "espwifi"
                }`
              : "Not configured")
          }
        />
      )}
    </>
  );

  const editContent = (
    <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
      <TextField
        fullWidth
        size="small"
        label="Device Name"
        value={tempDeviceName}
        onChange={(e) => setTempDeviceName(e.target.value)}
        placeholder="Device name"
        helperText="Used as hostname and in AP SSID"
        error={
          tempDeviceName && !/^[a-z0-9-]+$/.test(tempDeviceName.toLowerCase())
        }
      />

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
            label="AP SSID Prefix"
            value={tempApSsid}
            onChange={(e) => setTempApSsid(e.target.value)}
            placeholder="ESP"
            helperText="Device name will be appended automatically"
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
