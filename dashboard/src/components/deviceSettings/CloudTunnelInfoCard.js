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
  TextField,
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
  const [tempMaxFps, setTempMaxFps] = useState(0);

  useEffect(() => {
    setTempEnabled(Boolean(config?.cloudTunnel?.enabled));
    const v = config?.cloudTunnel?.maxFps;
    setTempMaxFps(typeof v === "number" ? v : parseInt(v || 0, 10) || 0);
  }, [config?.cloudTunnel?.enabled, config?.cloudTunnel?.maxFps]);

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
        maxFps: Number.isFinite(tempMaxFps) ? tempMaxFps : 0,
      },
    };
    onSave(next);
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempEnabled(Boolean(config?.cloudTunnel?.enabled));
    const v = config?.cloudTunnel?.maxFps;
    setTempMaxFps(typeof v === "number" ? v : parseInt(v || 0, 10) || 0);
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
      {(info?.enabled || config?.cloudTunnel?.enabled) && (
        <InfoRow
          label="Max FPS (cloud-only):"
          value={
            (config?.cloudTunnel?.maxFps ?? info?.maxFps ?? 0) === 0
              ? "No cap"
              : String(config?.cloudTunnel?.maxFps ?? info?.maxFps)
          }
        />
      )}
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

      <TextField
        label="Max cloud FPS (camera)"
        type="number"
        size="small"
        value={tempMaxFps}
        disabled={!tempEnabled}
        onChange={(e) => {
          const n = parseInt(e.target.value || "0", 10);
          setTempMaxFps(Number.isFinite(n) ? n : 0);
        }}
        helperText="Applies only when viewing camera via cloud tunnel with no LAN clients. 0 = no cap."
        inputProps={{ min: 0, step: 1 }}
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
        title="Secure Cloud Tunnel"
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
        <DialogTitle>ESPWiFi Secure Cloud Tunnel</DialogTitle>
        <DialogContent dividers>
          <Typography variant="h5" color="text.primary" align="center">
            ESPWiFi Secure Cloud Tunnel keeps an outbound WebSocket connection
            from the device to wss://tnl.espwifi.io Secure Cloud Tunnel server
            so the dashboard can reach the device’s WebSocket endpoints over the
            public internet (no VPN or port-forwarding required). This is a
            secure and private connection and provides authentication and
            encryption.
          </Typography>
          <Typography variant="h7" color="text.secondary" align="center">
            The cloud tunnel server is hosted by ESPWiFi.io and is free to use.
            It is not required to use the cloud tunnel, but it is recommended if
            you want to access the device’s WebSocket endpoints over the public
            internet.
          </Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setAboutOpen(false)}>Close</Button>
        </DialogActions>
      </Dialog>
    </>
  );
}
