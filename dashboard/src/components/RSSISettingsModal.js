import React, { useState, useEffect, useRef } from "react";
import {
  FormControlLabel,
  RadioGroup,
  Radio,
  Typography,
  Box,
} from "@mui/material";
import SignalCellularAltIcon from "@mui/icons-material/SignalCellularAlt";
import SettingsModal from "./SettingsModal";

export default function RSSISettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
  onRSSIDataChange,
}) {
  // Remove internal modal state - use external open prop

  // RSSI settings state (for modal editing)
  const [displayMode, setDisplayMode] = useState("icon"); // "icon", "numbers"

  // RSSI is always enabled
  const savedEnabled = true;

  // RSSI data state
  const [rssiValue, setRssiValue] = useState(null);
  const wsRef = useRef(null);

  useEffect(() => {
    if (config?.rssi) {
      setDisplayMode(config.rssi.displayMode || "icon");
    }
  }, [config]);

  // WebSocket connection for RSSI data - auto-connect if RSSI is enabled
  useEffect(() => {
    if (savedEnabled) {
      // Add a delay to allow the backend to start the RSSI service
      const connectTimeout = setTimeout(() => {
        // Construct WebSocket URL
        let wsUrl = "/rssi";
        if (wsUrl.startsWith("/")) {
          const protocol =
            window.location.protocol === "https:" ? "wss:" : "ws:";
          const mdnsHostname = config?.mdns;
          const hostname = mdnsHostname
            ? `${mdnsHostname}.local`
            : window.location.hostname;
          const port =
            window.location.port && !mdnsHostname
              ? `:${window.location.port}`
              : "";
          wsUrl = `${protocol}//${hostname}${port}${wsUrl}`;
        }

        // Connect to RSSI WebSocket
        const ws = new WebSocket(wsUrl);
        wsRef.current = ws;

        ws.onopen = () => {
          if (onRSSIDataChange) {
            onRSSIDataChange(rssiValue, true);
          }
        };

        ws.onmessage = (event) => {
          try {
            // RSSI data comes as plain text (just the number)
            const rssiValue = parseInt(event.data);
            if (!isNaN(rssiValue)) {
              setRssiValue(rssiValue);
              if (onRSSIDataChange) {
                onRSSIDataChange(rssiValue, true);
              }
            }
          } catch (error) {
            console.error("Error parsing RSSI data:", error);
          }
        };

        ws.onerror = (error) => {
          console.error("RSSI WebSocket error:", error);
          if (onRSSIDataChange) {
            onRSSIDataChange(rssiValue, false);
          }
        };

        ws.onclose = (event) => {
          wsRef.current = null;
          if (onRSSIDataChange) {
            onRSSIDataChange(rssiValue, false);
          }

          // Only retry if RSSI is still enabled and it's not a normal closure
          if (event.code !== 1000 && savedEnabled) {
            setTimeout(() => {
              // Double-check that RSSI is still enabled before retrying
              if (savedEnabled && !wsRef.current) {
                // Retry connection
                const retryWs = new WebSocket(wsUrl);
                wsRef.current = retryWs;

                retryWs.onopen = () => {
                  // WebSocket retry connected
                };
                retryWs.onmessage = (event) => {
                  try {
                    // RSSI data comes as plain text (just the number)
                    const rssiValue = parseInt(event.data);
                    if (!isNaN(rssiValue)) {
                      setRssiValue(rssiValue);
                      if (onRSSIDataChange) {
                        onRSSIDataChange(rssiValue, true);
                      }
                    }
                  } catch (error) {
                    console.error("Error parsing RSSI data:", error);
                  }
                };
                retryWs.onerror = (error) => {
                  console.error("RSSI WebSocket retry error:", error);
                  if (onRSSIDataChange) {
                    onRSSIDataChange(rssiValue, false);
                  }
                };
                retryWs.onclose = () => {
                  wsRef.current = null;
                  if (onRSSIDataChange) {
                    onRSSIDataChange(rssiValue, false);
                  }
                };
              }
            }, 2000);
          }
        };
      }, 1000); // 1 second delay

      return () => {
        clearTimeout(connectTimeout);
        if (wsRef.current) {
          wsRef.current.close();
          wsRef.current = null;
        }
      };
    } else if (!savedEnabled) {
      // Disconnect if disabled
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
      setRssiValue(null);
    }
  }, [savedEnabled]);

  // Cleanup WebSocket on unmount
  useEffect(() => {
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, []);

  const handleCloseModal = () => {
    if (onClose) onClose();
  };

  // RSSI is always enabled, no need for enable/disable handler

  const handleDisplayModeChange = (event) => {
    const newDisplayMode = event.target.value;
    setDisplayMode(newDisplayMode);

    // Apply changes immediately
    const configToSave = {
      ...config,
      rssi: {
        enabled: true, // Always enabled
        displayMode: newDisplayMode,
      },
    };

    // Save to device immediately
    saveConfigToDevice(configToSave);

    // Close modal after selection
    handleCloseModal();
  };

  // No save button needed - changes apply immediately

  return (
    <SettingsModal
      open={open}
      onClose={handleCloseModal}
      title={
        <span
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: "8px",
            width: "100%",
          }}
        >
          <SignalCellularAltIcon color="primary" />
          RSSI
        </span>
      }
      actions={null}
    >
      <Box sx={{ marginTop: 1 }}>
        <RadioGroup
          value={displayMode}
          onChange={handleDisplayModeChange}
          sx={{
            "& .MuiRadio-root": {
              color: "primary.main",
            },
            "& .MuiRadio-root.Mui-checked": {
              color: "primary.main",
            },
          }}
        >
          <FormControlLabel
            value="icon"
            control={<Radio />}
            label="Icon (Signal Bars)"
          />
          <FormControlLabel
            value="numbers"
            control={<Radio />}
            label="Numbers (dBm)"
          />
        </RadioGroup>
      </Box>

      <Box
        sx={{
          marginTop: 2,
          padding: 2,
          backgroundColor: "rgba(71, 255, 240, 0.1)",
          borderRadius: 1,
        }}
      >
        <Typography variant="body2" color="primary.main">
          📶{" "}
          <a
            href="https://en.wikipedia.org/wiki/Received_signal_strength_indication"
            target="_blank"
            rel="noopener noreferrer"
            style={{ color: "inherit", textDecoration: "underline" }}
          >
            Received Signal Strength Indicator (RSSI)
          </a>
        </Typography>
        <Typography
          variant="caption"
          sx={{ marginTop: 1, color: "primary.main", display: "block" }}
        >
          WebSocket URL: ws://
          {config?.mdns ? `${config.mdns}.local` : window.location.hostname}:
          {window.location.port || 80}/rssi
        </Typography>
      </Box>
    </SettingsModal>
  );
}
