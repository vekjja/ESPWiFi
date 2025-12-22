import React, { useState, useEffect, useRef } from "react";
import {
  Box,
  Typography,
  Button,
  Divider,
  List,
  ListItem,
  ListItemText,
  Paper,
  Grid,
  Switch,
  FormControlLabel,
} from "@mui/material";
import {
  BluetoothDisabled as BluetoothDisabledIcon,
  Bluetooth as BluetoothIcon,
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
  bluetoothStatus,
  onStatusChange,
}) {
  const [loading, setLoading] = useState(false);
  const [enabled, setEnabled] = useState(config?.bluetooth?.enabled || false);
  const initializedRef = useRef(false);

  // Reset state when modal opens - load current config values
  // Only initialize once when modal opens, don't sync while modal is open
  useEffect(() => {
    if (open && !initializedRef.current) {
      setEnabled(config?.bluetooth?.enabled || false);
      initializedRef.current = true;
      if (onStatusChange) {
        onStatusChange();
      }
    } else if (!open) {
      // Reset the ref when modal closes so it can initialize again next time
      initializedRef.current = false;
    }
    // Only reset when modal opens/closes, not when config or onStatusChange changes
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

      if (onStatusChange) {
        setTimeout(() => onStatusChange(), 500);
      }
    } catch (err) {
      console.error("Error disconnecting Bluetooth:", err);
    } finally {
      setLoading(false);
    }
  };

  const handleToggleEnabled = (event) => {
    setEnabled(event.target.checked);
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
    if (onStatusChange) {
      setTimeout(() => onStatusChange(), 500);
    }
    onClose();
  };
  const connected = bluetoothStatus?.connected || false;
  const deviceName = bluetoothStatus?.deviceName || "";
  const address = bluetoothStatus?.address || "";

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
      <Box sx={{ mt: 2 }}>
        {/* Enable/Disable Bluetooth */}
        <FormControlLabel
          control={
            <Switch
              checked={enabled}
              onChange={handleToggleEnabled}
              disabled={loading || !deviceOnline}
            />
          }
          label="Enable Bluetooth"
          sx={{ mb: 2 }}
        />

        {!enabled ? (
          <Paper
            sx={{ p: 3, textAlign: "center", bgcolor: "background.default" }}
          >
            <Typography variant="body1" color="text.secondary">
              Bluetooth is disabled
            </Typography>
            <Typography
              variant="caption"
              color="text.secondary"
              sx={{ mt: 1, display: "block" }}
            >
              Enable Bluetooth to view status information
            </Typography>
          </Paper>
        ) : (
          <>
            {/* Connection Status */}
            <Box sx={{ mb: 3 }}>
              <List dense>
                <ListItem>
                  <ListItemText
                    primary="Status"
                    secondary={
                      <Typography
                        variant="body2"
                        color={connected ? "success.main" : "text.secondary"}
                        sx={{ fontWeight: connected ? 500 : 400 }}
                      >
                        {connected ? "Connected" : "Not Connected"}
                      </Typography>
                    }
                  />
                </ListItem>
                {deviceName && (
                  <ListItem>
                    <ListItemText
                      primary="Device Name"
                      secondary={deviceName}
                    />
                  </ListItem>
                )}
                {address && (
                  <ListItem>
                    <ListItemText primary="MAC Address" secondary={address} />
                  </ListItem>
                )}
                {bluetoothStatus?.installed !== undefined && (
                  <ListItem>
                    <ListItemText
                      primary="Bluetooth Support"
                      secondary={
                        bluetoothStatus.installed
                          ? "Installed"
                          : "Not Available"
                      }
                    />
                  </ListItem>
                )}
              </List>
              {connected && (
                <Button
                  variant="outlined"
                  color="error"
                  startIcon={<BluetoothDisabledIcon />}
                  onClick={handleDisconnect}
                  disabled={loading}
                  sx={{ mt: 2 }}
                >
                  Disconnect
                </Button>
              )}
            </Box>

            <Divider sx={{ my: 3 }} />

            {/* BLE Service Information */}
            <Box sx={{ mb: 2 }}>
              <Typography variant="h6" gutterBottom>
                BLE Service Information
              </Typography>
              <Paper
                variant="outlined"
                sx={{ p: 2, bgcolor: "background.default" }}
              >
                <Grid container spacing={2}>
                  <Grid item xs={12}>
                    <Typography
                      variant="caption"
                      color="text.secondary"
                      display="block"
                    >
                      Service UUID
                    </Typography>
                    <Typography
                      variant="body2"
                      sx={{ fontFamily: "monospace" }}
                    >
                      0000ff00-0000-1000-8000-00805f9b34fb
                    </Typography>
                  </Grid>
                  <Grid item xs={12} sm={6}>
                    <Typography
                      variant="caption"
                      color="text.secondary"
                      display="block"
                    >
                      TX Characteristic
                    </Typography>
                    <Typography
                      variant="body2"
                      sx={{ fontFamily: "monospace" }}
                    >
                      0000ff01-0000-1000-8000-00805f9b34fb
                    </Typography>
                  </Grid>
                  <Grid item xs={12} sm={6}>
                    <Typography
                      variant="caption"
                      color="text.secondary"
                      display="block"
                    >
                      RX Characteristic
                    </Typography>
                    <Typography
                      variant="body2"
                      sx={{ fontFamily: "monospace" }}
                    >
                      0000ff02-0000-1000-8000-00805f9b34fb
                    </Typography>
                  </Grid>
                </Grid>
              </Paper>
            </Box>
          </>
        )}
      </Box>
    </SettingsModal>
  );
}
