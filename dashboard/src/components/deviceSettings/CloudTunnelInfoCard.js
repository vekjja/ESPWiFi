/**
 * @file CloudTunnelInfoCard.js
 * @brief Cloud tunnel settings + status card
 *
 * Shows cloud tunnel configuration and runtime connection status as reported by
 * /api/info, and allows enabling/disabling the feature.
 */

import React, { useEffect, useMemo, useState } from "react";
import {
  Box,
  Button,
  Chip,
  FormControlLabel,
  Switch,
  TextField,
} from "@mui/material";
import CloudIcon from "@mui/icons-material/Cloud";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";

export default function CloudTunnelInfoCard({ config, deviceInfo, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [tempEnabled, setTempEnabled] = useState(false);
  const [tempBaseUrl, setTempBaseUrl] = useState("");

  useEffect(() => {
    setTempEnabled(Boolean(config?.cloudTunnel?.enabled));
    setTempBaseUrl(config?.cloudTunnel?.baseUrl || "");
  }, [config?.cloudTunnel?.enabled, config?.cloudTunnel?.baseUrl]);

  const info = deviceInfo?.cloudTunnel || {};
  const endpoints = info?.endpoints || {};
  const cam = endpoints?.camera || null;

  const anyConnected = useMemo(() => {
    const vals = Object.values(endpoints || {});
    return vals.some((e) => Boolean(e?.cloudConnected));
  }, [endpoints]);

  const handleSave = () => {
    const next = {
      cloudTunnel: {
        ...(config?.cloudTunnel || {}),
        enabled: tempEnabled,
        baseUrl: tempBaseUrl,
      },
    };
    onSave(next);
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempEnabled(Boolean(config?.cloudTunnel?.enabled));
    setTempBaseUrl(config?.cloudTunnel?.baseUrl || "");
    setIsEditing(false);
  };

  const statusChip = (
    <Chip
      size="small"
      label={
        info?.enabled
          ? anyConnected
            ? "Enabled (connected)"
            : "Enabled (disconnected)"
          : "Disabled"
      }
      color={info?.enabled ? (anyConnected ? "success" : "warning") : "default"}
      sx={{ mt: 0.5 }}
    />
  );

  const viewContent = (
    <>
      <InfoRow label="Status:" value={statusChip} />
      <InfoRow
        label="Base URL:"
        value={info?.baseUrl || config?.cloudTunnel?.baseUrl || "—"}
      />
      {cam && (
        <Box sx={{ mt: 1 }}>
          <InfoRow label="Camera:" value={cam.uri || "/ws/camera"} />
          <InfoRow
            label="Camera tunnel:"
            value={
              <Chip
                size="small"
                label={
                  cam.cloudConnected
                    ? "Connected"
                    : cam.cloudEnabled
                    ? "Disconnected"
                    : "Disabled"
                }
                color={
                  cam.cloudConnected
                    ? "success"
                    : cam.cloudEnabled
                    ? "warning"
                    : "default"
                }
                sx={{ mt: 0.5 }}
              />
            }
          />
        </Box>
      )}
    </>
  );

  const editContent = (
    <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
      <FormControlLabel
        control={
          <Switch
            checked={tempEnabled}
            onChange={(e) => setTempEnabled(e.target.checked)}
            color="primary"
          />
        }
        label="Enable Cloud Tunnel"
      />

      <TextField
        fullWidth
        size="small"
        label="Tunnel Base URL"
        value={tempBaseUrl}
        onChange={(e) => setTempBaseUrl(e.target.value)}
        placeholder="wss://tnl.espwifi.io"
        helperText="Example: wss://tnl.espwifi.io"
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
          Save
        </Button>
      </Box>
    </Box>
  );

  return (
    <InfoCard
      title="Cloud Tunnel"
      icon={CloudIcon}
      editable
      isEditing={isEditing}
      onEdit={() => setIsEditing(true)}
      editContent={editContent}
    >
      {viewContent}
    </InfoCard>
  );
}
