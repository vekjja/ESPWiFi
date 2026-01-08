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
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  FormControlLabel,
  IconButton,
  Switch,
  Tooltip,
  Typography,
} from "@mui/material";
import CloudIcon from "@mui/icons-material/Cloud";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import InfoOutlinedIcon from "@mui/icons-material/InfoOutlined";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";
import MaskedValueField from "../common/MaskedValueField";

export default function CloudTunnelInfoCard({ config, deviceInfo, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [aboutOpen, setAboutOpen] = useState(false);
  const [tempEnabled, setTempEnabled] = useState(false);

  useEffect(() => {
    setTempEnabled(Boolean(config?.cloudTunnel?.enabled));
  }, [config?.cloudTunnel?.enabled]);

  const info = deviceInfo?.cloudTunnel || {};
  const endpoints = info?.endpoints || {};

  const anyConnected = useMemo(() => {
    const vals = Object.values(endpoints || {});
    return vals.some((e) => Boolean(e?.cloudConnected));
  }, [endpoints]);

  const handleSave = () => {
    const next = {
      cloudTunnel: {
        ...(config?.cloudTunnel || {}),
        enabled: tempEnabled,
      },
    };
    onSave(next);
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempEnabled(Boolean(config?.cloudTunnel?.enabled));
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

  const storedToken = useMemo(() => {
    // Prefer device config token (authoritative). Fall back to dashboard storage.
    const cfgTok = config?.auth?.token || "";
    if (cfgTok && String(cfgTok).trim() !== "") {
      return String(cfgTok).trim();
    }
    const t = localStorage.getItem("espwifi_auth_token") || "";
    return t && t !== "null" && t !== "undefined" ? t : "";
  }, [config?.auth?.token]);

  const withToken = (uiUrl) => {
    if (!uiUrl) return "";
    // Prefer wss:// when the server gives https:// URLs.
    let u = String(uiUrl);
    if (u.startsWith("https://")) u = "wss://" + u.slice("https://".length);
    if (u.startsWith("http://")) u = "ws://" + u.slice("http://".length);
    if (!storedToken) return "";
    if (u.includes("token=")) return u;
    return (
      u +
      (u.includes("?") ? "&" : "?") +
      "token=" +
      encodeURIComponent(storedToken)
    );
  };

  const viewContent = (
    <>
      <InfoRow label="Status:" value={statusChip} />
      <InfoRow
        label="Base URL:"
        value={info?.baseUrl || config?.cloudTunnel?.baseUrl || "—"}
      />
      {Object.keys(endpoints || {}).length > 0 && (
        <Box sx={{ mt: 1, display: "flex", flexDirection: "column", gap: 2 }}>
          {Object.entries(endpoints).map(([name, ep]) => {
            const title = String(name || "endpoint");
            const uri = ep?.uri || "—";
            const cloudConnected = Boolean(ep?.cloudConnected);
            const cloudEnabled = Boolean(ep?.cloudEnabled);
            const uiUrl = ep?.ui_ws_url || "";
            const uiUrlWithToken = withToken(uiUrl);
            // If the URL contains a token, treat it as sensitive regardless of
            // connection state (disconnected should not reveal the token).
            const blurSensitive = Boolean(uiUrlWithToken);
            return (
              <Box key={name}>
                <InfoRow label={title} value={uri} />
                <InfoRow
                  label={`${title} tunnel:`}
                  value={
                    <Chip
                      size="small"
                      label={
                        cloudConnected
                          ? "Connected"
                          : cloudEnabled
                          ? "Disconnected"
                          : "Disabled"
                      }
                      color={
                        cloudConnected
                          ? "success"
                          : cloudEnabled
                          ? "warning"
                          : "default"
                      }
                      sx={{ mt: 0.5 }}
                    />
                  }
                />
                {cloudEnabled && uiUrl && uiUrl.trim() !== "" && (
                  <>
                    <InfoRow
                      label="Connection URL:"
                      value={
                        <MaskedValueField
                          value={
                            uiUrlWithToken ||
                            "— (token not available yet; connect/auth to the device first)"
                          }
                          blur={blurSensitive}
                          defaultShow={false}
                        />
                      }
                    />
                    <Box
                      sx={{
                        display: "flex",
                        justifyContent: "flex-end",
                        mt: 1,
                      }}
                    >
                      <Button
                        size="small"
                        variant="outlined"
                        disabled={!uiUrlWithToken}
                        onClick={() => {
                          if (navigator.clipboard?.writeText) {
                            navigator.clipboard.writeText(uiUrlWithToken);
                          }
                        }}
                      >
                        Copy URL
                      </Button>
                    </Box>
                  </>
                )}
              </Box>
            );
          })}
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
    <>
      <InfoCard
        title="Cloud Tunnel"
        icon={CloudIcon}
        headerActions={
          <Tooltip title="What is Cloud Tunnel?">
            <IconButton size="small" onClick={() => setAboutOpen(true)}>
              <InfoOutlinedIcon fontSize="small" />
            </IconButton>
          </Tooltip>
        }
        editable
        isEditing={isEditing}
        onEdit={() => setIsEditing(true)}
        editContent={editContent}
      >
        {viewContent}
      </InfoCard>

      <Dialog
        open={aboutOpen}
        onClose={() => setAboutOpen(false)}
        maxWidth="sm"
        fullWidth
      >
        <DialogTitle>ESPWiFi.iO Cloud Tunnel</DialogTitle>
        <DialogContent dividers>
          <Typography variant="body2" color="text.secondary">
            Cloud Tunnel keeps an outbound WebSocket connection from the device
            to wss://tnl.espwifi.io cloud so the dashboard can reach the
            device’s WebSocket endpoints over the public internet (no VPN or
            port-forwarding required).
          </Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setAboutOpen(false)}>Close</Button>
        </DialogActions>
      </Dialog>
    </>
  );
}
