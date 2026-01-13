/**
 * @file CloudInfoCard.js
 * @brief Cloud settings + status card
 *
 * Shows cloud configuration and runtime connection status as reported by
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
import { safeGetItem } from "../../utils/storageUtils";
import QRCode from "qrcode";

export default function CloudInfoCard({ config, deviceInfo, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [aboutOpen, setAboutOpen] = useState(false);
  const [qrOpen, setQrOpen] = useState(false);
  const [tempEnabled, setTempEnabled] = useState(false);

  useEffect(() => {
    setTempEnabled(Boolean(config?.cloud?.enabled));
  }, [config?.cloud?.enabled]);

  const info = deviceInfo?.cloud || {};
  const endpoints = info?.endpoints || {};
  const pairing = deviceInfo?.pairing || {};
  const claimCode = String(pairing?.claim_code || "").trim();
  const claimUrl = claimCode ? `https://espwifi.io/?claim=${claimCode}` : "";
  const [qrDataUrl, setQrDataUrl] = useState("");

  useEffect(() => {
    let alive = true;
    if (!qrOpen || !claimUrl) {
      setQrDataUrl("");
      return () => {};
    }
    QRCode.toDataURL(
      claimUrl,
      { width: 240, margin: 1, errorCorrectionLevel: "M" },
      (err, url) => {
        if (!alive) return;
        if (err) {
          setQrDataUrl("");
          return;
        }
        setQrDataUrl(url || "");
      }
    );
    return () => {
      alive = false;
    };
  }, [qrOpen, claimUrl]);

  const anyConnected = useMemo(() => {
    const vals = Object.values(endpoints || {});
    return vals.some((e) => Boolean(e?.cloudConnected));
  }, [endpoints]);

  const handleSave = () => {
    const next = {
      cloud: {
        ...(config?.cloud || {}),
        enabled: tempEnabled,
        // Cloud toggle = tunnel all available websockets.
        tunnelAll: tempEnabled ? true : false,
        // Allow non-control tunnels when cloud is enabled.
      },
    };

    // If cloud is enabled and the device has a camera, enable it so /ws/camera can tunnel.
    if (tempEnabled && config?.camera?.installed !== false) {
      next.camera = { ...(config?.camera || {}), enabled: true };
    }

    onSave(next);
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempEnabled(Boolean(config?.cloud?.enabled));
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
    const t = safeGetItem("espwifi_auth_token") || "";
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
        value={info?.baseUrl || config?.cloud?.baseUrl || "—"}
      />
      <InfoRow
        label="iPhone pairing:"
        value={
          claimCode ? (
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <MaskedValueField value={claimCode} blur={false} defaultShow />
              <Box sx={{ display: "flex", justifyContent: "flex-end", gap: 1 }}>
                <Button
                  size="small"
                  variant="outlined"
                  onClick={() => {
                    if (navigator.clipboard?.writeText) {
                      navigator.clipboard.writeText(claimCode);
                    }
                  }}
                >
                  Copy code
                </Button>
                <Button
                  size="small"
                  variant="contained"
                  onClick={() => setQrOpen(true)}
                >
                  Show QR
                </Button>
              </Box>
            </Box>
          ) : (
            "—"
          )
        }
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
        label="Enable Cloud"
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
        title="ESPWiFi Cloud"
        icon={CloudIcon}
        headerActions={
          <Tooltip title="What is ESPWiFi Cloud?">
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
        open={qrOpen}
        onClose={() => setQrOpen(false)}
        maxWidth="xs"
        fullWidth
      >
        <DialogTitle>Pair (iPhone)</DialogTitle>
        <DialogContent dividers>
          <Typography variant="body2" sx={{ opacity: 0.9, mb: 1 }}>
            Scan this QR on iPhone to open <code>espwifi.io</code> and claim the
            device.
          </Typography>
          {qrDataUrl ? (
            <Box sx={{ display: "flex", justifyContent: "center", my: 1 }}>
              <img
                src={qrDataUrl}
                alt="Pairing QR"
                style={{ width: 240, height: 240 }}
              />
            </Box>
          ) : (
            <Typography variant="body2" sx={{ opacity: 0.75 }}>
              Generating QR…
            </Typography>
          )}
          <Typography
            variant="caption"
            sx={{ display: "block", mt: 1, opacity: 0.75 }}
          >
            Claim URL: {claimUrl || "—"}
          </Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setQrOpen(false)}>Close</Button>
        </DialogActions>
      </Dialog>

      <Dialog
        open={aboutOpen}
        onClose={() => setAboutOpen(false)}
        maxWidth="sm"
        fullWidth
      >
        <DialogTitle>ESPWiFi Cloud</DialogTitle>
        <DialogContent dividers>
          <Typography variant="h5" color="text.primary" align="center">
            ESPWiFi Cloud keeps an outbound WebSocket connection from the device
            to wss://tnl.espwifi.io cloud server so the dashboard can reach the
            device's WebSocket endpoints over the public internet (no VPN or
            port-forwarding required). This is a secure and private connection
            and provides authentication and encryption.
          </Typography>
          <Typography variant="h7" color="text.secondary" align="center">
            The cloud server is hosted by ESPWiFi.io and is free to use. It is
            not required to use the cloud, but it is recommended if you want to
            access the device's WebSocket endpoints over the public internet.
          </Typography>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setAboutOpen(false)}>Close</Button>
        </DialogActions>
      </Dialog>
    </>
  );
}
