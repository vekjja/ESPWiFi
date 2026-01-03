/**
 * @file WifiPowerInfoCard.js
 * @brief WiFi power settings card with TX power and power save mode
 *
 * Displays and allows editing of TX power and power save configuration
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
  Grid,
} from "@mui/material";
import BoltIcon from "@mui/icons-material/Bolt";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";
import { formatUptime } from "../../utils/formatUtils";

/**
 * WifiPowerInfoCard Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.config - Device configuration
 * @param {Object} props.deviceInfo - Device information
 * @param {Function} props.onSave - Callback to save configuration
 * @returns {JSX.Element} The rendered WiFi power card
 */
export default function WifiPowerInfoCard({ config, deviceInfo, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [tempTxPower, setTempTxPower] = useState("");
  const [tempPowerSave, setTempPowerSave] = useState("none");

  useEffect(() => {
    if (config) {
      setTempTxPower(config.wifi?.power?.txPower?.toString() || "19.5");
      setTempPowerSave(config.wifi?.power?.powerSave || "none");
    }
  }, [config]);

  const handleSave = () => {
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
      onSave(powerConfig);
      setIsEditing(false);
    }
  };

  const handleCancel = () => {
    setTempTxPower(config?.wifi?.power?.txPower?.toString() || "19.5");
    setTempPowerSave(config?.wifi?.power?.powerSave || "none");
    setIsEditing(false);
  };

  if (!deviceInfo.wifi_power) {
    return null;
  }

  const viewContent = (
    <>
      <Grid container spacing={2}>
        <Grid item xs={12} sm={3}>
          <InfoRow
            label="TX Power:"
            value={`${
              deviceInfo.wifi_power.configured?.txPower?.toFixed(1) || "N/A"
            } dBm`}
          />
        </Grid>

        {deviceInfo.wifi_power.actual?.txPower && (
          <Grid item xs={12} sm={3}>
            <InfoRow
              label="Actual TX Power:"
              value={`${deviceInfo.wifi_power.actual.txPower.toFixed(1)} dBm`}
            />
          </Grid>
        )}

        <Grid item xs={12} sm={3}>
          <InfoRow
            label="Power Save:"
            value={
              <Chip
                label={deviceInfo.wifi_power.configured?.powerSave || "none"}
                size="small"
                color={
                  deviceInfo.wifi_power.configured?.powerSave === "none"
                    ? "default"
                    : "success"
                }
                sx={{ mt: 0.5 }}
              />
            }
          />
        </Grid>

        {deviceInfo.uptime !== undefined && (
          <Grid item xs={12} sm={3}>
            <InfoRow label="Uptime:" value={formatUptime(deviceInfo.uptime)} />
          </Grid>
        )}
      </Grid>
    </>
  );

  const editContent = (
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
          <MenuItem value="none">None - Maximum performance</MenuItem>
          <MenuItem value="min">Minimum - Saves most power</MenuItem>
          <MenuItem value="max">Maximum - Balance power/performance</MenuItem>
        </Select>
      </FormControl>

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
          Save
        </Button>
      </Box>
    </Box>
  );

  return (
    <InfoCard
      title="WiFi Power"
      icon={BoltIcon}
      editable
      isEditing={isEditing}
      onEdit={() => setIsEditing(true)}
      editContent={editContent}
    >
      {viewContent}
    </InfoCard>
  );
}
