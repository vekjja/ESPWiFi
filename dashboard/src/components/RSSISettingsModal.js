import React, { useState, useEffect, useRef } from "react";
import { Typography, Box } from "@mui/material";
import SignalCellularAltIcon from "@mui/icons-material/SignalCellularAlt";
import SettingsModal from "./SettingsModal";
import { buildWebSocketUrl } from "../utils/apiUtils";

export default function RSSISettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
  onRSSIDataChange,
}) {
  // RSSI data state
  const [rssiValue, setRssiValue] = useState(null);
  const wsRef = useRef(null);

  // WebSocket connection for RSSI data - always connect
  useEffect(() => {
    // Add a delay to allow the backend to start the RSSI service
    const connectTimeout = setTimeout(() => {
      // Construct WebSocket URL
      const mdnsHostname = config?.deviceName;
      const wsUrl = buildWebSocketUrl("/ws/rssi", mdnsHostname);

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

        // Only retry if it's not a normal closure
        if (event.code !== 1000) {
          setTimeout(() => {
            // Double-check before retrying
            if (!wsRef.current) {
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
  }, []);

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
          <br /> <br />
          WebSocket URL: ws://
          {config?.hostname
            ? `${config.hostname}`
            : config?.deviceName
            ? `${config.deviceName}`
            : window.location.hostname}
          :{window.location.port || 80}/ws/rssi
        </Typography>
        <Typography
          variant="caption"
          sx={{ marginTop: 1, color: "primary.main", display: "block" }}
        >
          RSSI (Received Signal Strength Indicator) is a device-reported measure
          of radio signal power at the receiver. It’s often shown in dBm
          (negative values), where numbers closer to 0 mean stronger signal:
          around −30 dBm is excellent, about −67 dBm is solid for Wi‑Fi/VoIP,
          −75 to −85 dBm is weak, and below −90 dBm is likely unusable. RSSI
          isn’t an absolute standard—scales vary by chipset—so treat it as a
          relative indicator for link quality, range, and placement.
        </Typography>
      </Box>
    </SettingsModal>
  );
}
