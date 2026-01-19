import React, { useState } from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  TextField,
  Alert,
  Box,
  Typography,
} from "@mui/material";
import { redeemClaimCode } from "../utils/apiUtils";

/**
 * Prompts user to enter the claim code displayed on their device
 * after BLE pairing, so we can get the cloud tunnel WebSocket URL.
 */
export default function ClaimCodePrompt({ open, device, onSuccess, onSkip }) {
  const [code, setCode] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const handleSubmit = async () => {
    if (!code.trim()) {
      setError("Please enter a claim code");
      return;
    }

    setBusy(true);
    setError("");

    try {
      const cloudBaseUrl =
        device?.cloudTunnel?.baseUrl || "https://cloud.espwifi.io";
      const tunnel = device?.cloudTunnel?.tunnel || "ws_control";

      const result = await redeemClaimCode(
        code.trim().toUpperCase(),
        cloudBaseUrl,
        tunnel
      );

      if (!result?.ui_ws_url) {
        throw new Error("Invalid response from cloud broker");
      }

      // Update device record with cloud tunnel info
      const updatedDevice = {
        ...device,
        cloudTunnel: {
          enabled: true,
          baseUrl: cloudBaseUrl,
          wsUrl: result.ui_ws_url,
          authToken: result.ui_auth_token,
          tunnel,
          needsClaim: false,
        },
      };

      onSuccess?.(updatedDevice);
    } catch (err) {
      console.error("[ClaimCodePrompt] Error redeeming claim code:", err);
      setError(err.message || "Failed to redeem claim code");
    } finally {
      setBusy(false);
    }
  };

  const handleSkip = () => {
    onSkip?.();
  };

  return (
    <Dialog open={open} maxWidth="sm" fullWidth>
      <DialogTitle sx={{ fontWeight: 800 }}>
        Enter Device Claim Code
      </DialogTitle>
      <DialogContent dividers>
        <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
          <Alert severity="info">
            Your device has been paired via Bluetooth and is now connecting to
            the cloud. To access it remotely, please enter the 6-character claim
            code displayed on your device's screen or serial output.
          </Alert>

          <Typography variant="body2" color="text.secondary">
            The claim code allows you to securely connect to your device through
            the cloud tunnel. If you don't see a claim code, check your device's
            serial monitor or OLED display.
          </Typography>

          <TextField
            label="Claim Code"
            value={code}
            onChange={(e) => setCode(e.target.value.toUpperCase())}
            placeholder="ABC123"
            autoComplete="off"
            inputProps={{ maxLength: 6 }}
            disabled={busy}
            error={!!error}
            helperText={error || "6-character alphanumeric code"}
            autoFocus
            onKeyPress={(e) => {
              if (e.key === "Enter") {
                handleSubmit();
              }
            }}
          />
        </Box>
      </DialogContent>
      <DialogActions>
        <Button onClick={handleSkip} disabled={busy}>
          Skip (Local Only)
        </Button>
        <Button
          onClick={handleSubmit}
          variant="contained"
          disabled={busy || !code.trim()}
        >
          {busy ? "Verifying..." : "Submit"}
        </Button>
      </DialogActions>
    </Dialog>
  );
}
