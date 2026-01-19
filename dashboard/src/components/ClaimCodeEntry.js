import React, { useState } from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  TextField,
  Box,
  Typography,
  Alert,
  CircularProgress,
  IconButton,
} from "@mui/material";
import CloseIcon from "@mui/icons-material/Close";
import { redeemClaimCode } from "../utils/apiUtils";

/**
 * ClaimCodeEntry - Dialog for entering a device claim code
 *
 * Supports:
 * - Manual entry of 6-character claim code
 * - Automatic device pairing via cloud tunnel
 */
export default function ClaimCodeEntry({
  open,
  onClose,
  onDeviceClaimed,
  existingDevices = [], // Array of existing devices to get cloud URL from
}) {
  const [claimCode, setClaimCode] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  // Get cloud base URL from an existing device, or use default
  const cloudBaseUrl =
    existingDevices.find((d) => d?.cloudTunnel?.baseUrl)?.cloudTunnel
      ?.baseUrl || "https://cloud.espwifi.io";

  const handleSubmit = async () => {
    setError("");

    // Validate claim code format (6 alphanumeric characters)
    const code = claimCode.trim().toUpperCase();
    if (!/^[A-Z0-9]{6}$/.test(code)) {
      setError("Claim code must be 6 alphanumeric characters");
      return;
    }

    setBusy(true);

    try {
      console.log("[ClaimCodeEntry] Redeeming claim code:", code);
      console.log("[ClaimCodeEntry] Using cloud base URL:", cloudBaseUrl);

      // Call cloud API to redeem claim code
      const result = await redeemClaimCode(code, cloudBaseUrl);

      console.log("[ClaimCodeEntry] Claim result:", result);

      if (!result.ok) {
        throw new Error(result.error || "Failed to redeem claim code");
      }

      // Create device record with cloud tunnel info
      const deviceRecord = {
        id: result.device_id,
        name: result.device_id, // Will be updated once we connect
        hostname: result.device_id,
        deviceId: result.device_id,
        authToken: result.token,
        cloudTunnel: {
          enabled: true,
          baseUrl: cloudBaseUrl, // Use the cloud URL from existing devices or default
          wsUrl: result.ui_ws_url,
        },
        lastSeenAtMs: Date.now(),
      };

      console.log("[ClaimCodeEntry] Device claimed:", deviceRecord);

      // Notify parent
      onDeviceClaimed?.(deviceRecord, {
        claimCode: code,
        tunnel: result.tunnel,
        uiWsUrl: result.ui_ws_url,
      });

      // Close dialog
      handleClose();
    } catch (e) {
      console.error("[ClaimCodeEntry] Error:", e);
      setError(e?.message || String(e));
      setBusy(false);
    }
  };

  const handleClose = () => {
    setClaimCode("");
    setError("");
    setBusy(false);
    onClose?.();
  };

  return (
    <Dialog open={open} onClose={handleClose} maxWidth="sm" fullWidth>
      <DialogTitle>
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "space-between",
          }}
        >
          <Typography variant="h6">Enter Claim Code</Typography>
          <IconButton onClick={handleClose} size="small">
            <CloseIcon />
          </IconButton>
        </Box>
      </DialogTitle>

      <DialogContent>
        <Box sx={{ display: "flex", flexDirection: "column", gap: 2, pt: 1 }}>
          <Alert severity="info">
            Enter the 6-character claim code displayed on your ESPWiFi device to
            pair it for remote access via the cloud.
          </Alert>

          <TextField
            label="Claim Code"
            value={claimCode}
            onChange={(e) => {
              // Auto-uppercase and limit to 6 characters
              const val = e.target.value
                .toUpperCase()
                .replace(/[^A-Z0-9]/g, "");
              setClaimCode(val.slice(0, 6));
            }}
            placeholder="ABC123"
            autoComplete="off"
            fullWidth
            autoFocus
            disabled={busy}
            inputProps={{
              maxLength: 6,
              style: {
                textTransform: "uppercase",
                letterSpacing: "0.1em",
                fontSize: "1.5rem",
                textAlign: "center",
              },
            }}
            helperText="6 alphanumeric characters (e.g., ABC123)"
          />

          {busy && (
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
              <CircularProgress size={20} />
              <Typography variant="body2">Claiming device...</Typography>
            </Box>
          )}

          {error && (
            <Alert severity="error" onClose={() => setError("")}>
              {error}
            </Alert>
          )}
        </Box>
      </DialogContent>

      <DialogActions>
        <Button onClick={handleClose} disabled={busy}>
          Cancel
        </Button>
        <Button
          onClick={handleSubmit}
          variant="contained"
          disabled={busy || claimCode.length !== 6}
        >
          Claim Device
        </Button>
      </DialogActions>
    </Dialog>
  );
}
