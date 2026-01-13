import React, { useState } from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Button,
  List,
  ListItemButton,
  ListItemText,
  IconButton,
  Box,
  Typography,
} from "@mui/material";
import DeleteOutlineIcon from "@mui/icons-material/DeleteOutline";
import AddIcon from "@mui/icons-material/Add";
import QrCodeScannerIcon from "@mui/icons-material/QrCodeScanner";
import ClaimCodeEntry from "./ClaimCodeEntry";

function formatWhen(ms) {
  if (!ms) return "never";
  try {
    const d = new Date(ms);
    return d.toLocaleString();
  } catch {
    return "unknown";
  }
}

export default function DevicePickerDialog({
  open,
  onClose,
  devices,
  selectedId,
  onSelect,
  onRemove,
  onPairNew,
  onDeviceClaimed,
}) {
  const [showClaimCodeEntry, setShowClaimCodeEntry] = useState(false);

  const handleDeviceClaimed = (deviceRecord, details) => {
    setShowClaimCodeEntry(false);
    onDeviceClaimed?.(deviceRecord, details);
  };

  return (
    <>
      <Dialog open={open} onClose={onClose} fullWidth maxWidth="sm">
        <DialogTitle sx={{ fontWeight: 800 }}>Devices</DialogTitle>
        <DialogContent dividers>
          {(!devices || devices.length === 0) && (
            <Box sx={{ py: 1 }}>
              <Typography variant="body1" sx={{ fontWeight: 700, mb: 0.5 }}>
                No saved devices
              </Typography>
              <Typography variant="body2" sx={{ opacity: 0.9 }}>
                Pair a device using BLE or enter a claim code for cloud access.
              </Typography>
            </Box>
          )}

          {devices && devices.length > 0 && (
            <List sx={{ p: 0 }}>
              {devices.map((d) => {
                const primary = d?.name || d?.id || "Unknown device";
                const secondaryParts = [];
                if (d?.deviceId) secondaryParts.push(`deviceId: ${d.deviceId}`);
                if (d?.hostname) secondaryParts.push(`host: ${d.hostname}`);
                if (d?.cloudTunnel?.enabled) secondaryParts.push("☁️ cloud");
                secondaryParts.push(
                  `last seen: ${formatWhen(d?.lastSeenAtMs)}`
                );
                const secondary = secondaryParts.join(" • ");
                const isSelected = String(d?.id) === String(selectedId);

                return (
                  <Box
                    key={String(d?.id)}
                    sx={{
                      display: "flex",
                      alignItems: "center",
                      gap: 1,
                      borderRadius: 2,
                      mb: 1,
                      bgcolor: isSelected
                        ? "rgba(71, 255, 240, 0.08)"
                        : "transparent",
                      border: "1px solid",
                      borderColor: isSelected ? "primary.main" : "divider",
                    }}
                  >
                    <ListItemButton
                      onClick={() => onSelect?.(d)}
                      sx={{ borderRadius: 2 }}
                    >
                      <ListItemText
                        primary={
                          <Typography sx={{ fontWeight: 800 }}>
                            {primary}
                          </Typography>
                        }
                        secondary={secondary}
                        secondaryTypographyProps={{ sx: { opacity: 0.85 } }}
                      />
                    </ListItemButton>
                    <IconButton
                      onClick={() => onRemove?.(d)}
                      aria-label="Remove device"
                      sx={{ mr: 1 }}
                    >
                      <DeleteOutlineIcon />
                    </IconButton>
                  </Box>
                );
              })}
            </List>
          )}
        </DialogContent>
        <DialogActions sx={{ px: 2, py: 1.5, gap: 1 }}>
          <Button onClick={onClose} color="inherit">
            Close
          </Button>
          <Button
            onClick={() => setShowClaimCodeEntry(true)}
            variant="outlined"
            startIcon={<QrCodeScannerIcon />}
          >
            Enter claim code
          </Button>
          <Button
            onClick={onPairNew}
            variant="contained"
            startIcon={<AddIcon />}
          >
            Pair via BLE
          </Button>
        </DialogActions>
      </Dialog>

      <ClaimCodeEntry
        open={showClaimCodeEntry}
        onClose={() => setShowClaimCodeEntry(false)}
        onDeviceClaimed={handleDeviceClaimed}
        existingDevices={devices}
      />
    </>
  );
}
