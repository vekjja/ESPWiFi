/**
 * @file NetworkInfoCard.js
 * @brief Network information display card
 *
 * Displays IP address, hostname, mDNS hostname, MAC address
 * Also allows editing device name (display + mDNS hostname seed)
 */

import React, { useEffect, useState } from "react";
import { Box, Button, Grid, TextField } from "@mui/material";
import LeakAddIcon from "@mui/icons-material/LeakAdd";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";

/**
 * NetworkInfoCard Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.deviceInfo - Device network information
 * @param {Object} props.config - Device configuration
 * @param {Function} props.onSave - Callback to save configuration
 * @returns {JSX.Element} The rendered network info card
 */
export default function NetworkInfoCard({ deviceInfo, config, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [tempDeviceName, setTempDeviceName] = useState("");

  useEffect(() => {
    setTempDeviceName(config?.deviceName || "");
  }, [config?.deviceName]);

  const handleSave = () => {
    const trimmed = tempDeviceName.trim();
    onSave({ deviceName: trimmed });
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempDeviceName(config?.deviceName || "");
    setIsEditing(false);
  };

  return (
    <InfoCard
      title="Network"
      icon={LeakAddIcon}
      editable
      isEditing={isEditing}
      onEdit={() => setIsEditing(true)}
      gridSize={{ xs: 12 }}
      editContent={
        <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
          <TextField
            fullWidth
            size="small"
            label="Device Name"
            value={tempDeviceName}
            onChange={(e) => setTempDeviceName(e.target.value)}
            placeholder="Device name"
            helperText="Used for display and mDNS hostname (.local)"
            error={
              tempDeviceName &&
              !/^[a-z0-9-]+$/.test(tempDeviceName.toLowerCase())
            }
          />

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
              Save Device Name
            </Button>
          </Box>
        </Box>
      }
    >
      <Grid container spacing={2}>
        <Grid item xs={12} sm={4}>
          <InfoRow
            label="Device Name:"
            value={config?.deviceName || "ESPWiFi"}
          />
        </Grid>
        <Grid item xs={12} sm={4}>
          <InfoRow label="Hostname:" value={deviceInfo?.hostname || "N/A"} />
        </Grid>
        <Grid item xs={12} sm={4}>
          <InfoRow label="mDNS Hostname:" value={deviceInfo?.mdns || "N/A"} />
        </Grid>

        <Grid item xs={12} sm={4}>
          <InfoRow label="IP Address:" value={deviceInfo?.ip || "N/A"} />
        </Grid>
        <Grid item xs={12} sm={4}>
          <InfoRow label="MAC Address:" value={deviceInfo?.mac || "N/A"} />
        </Grid>
      </Grid>
    </InfoCard>
  );
}
