import React, { useState, useEffect, useRef } from "react";
import { Fab, Tooltip } from "@mui/material";
import {
  SignalCellularAlt as SignalCellularAltIcon,
  SignalCellularAlt1Bar as SignalCellularAlt1BarIcon,
  SignalCellularAlt2Bar as SignalCellularAlt2BarIcon,
} from "@mui/icons-material";
import RSSISettingsModal from "./RSSISettingsModal";

export default function RSSIButton({
  config,
  deviceOnline,
  saveConfig,
  saveConfigToDevice,
  onRSSIDataChange,
  rssiDisplayMode,
  getRSSIColor,
}) {
  const [modalOpen, setModalOpen] = useState(false);
  const [rssiValue, setRssiValue] = useState(null);
  const wsRef = useRef(null);

  // WebSocket connection for RSSI data - always connect when device is online
  useEffect(() => {
    if (deviceOnline) {
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

          // Only retry if device is still online
          if (event.code !== 1000 && deviceOnline) {
            setTimeout(() => {
              // Double-check that device is still online before retrying
              if (deviceOnline && !wsRef.current) {
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

                retryWs.onclose = (event) => {
                  wsRef.current = null;
                  if (onRSSIDataChange) {
                    onRSSIDataChange(rssiValue, false);
                  }
                };
              }
            }, 2000); // 2 second retry delay
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
    } else if (!deviceOnline) {
      // Disconnect if device offline
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
      setRssiValue(null);
    }
  }, [deviceOnline, config?.mdns]);

  // Cleanup WebSocket on unmount
  useEffect(() => {
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, []);

  // Get the appropriate signal icon based on RSSI value
  const getRSSIIconComponent = (rssiValue) => {
    if (rssiValue === null || rssiValue === undefined) {
      return <SignalCellularAltIcon />;
    }
    if (rssiValue >= -60) return <SignalCellularAltIcon />;
    if (rssiValue >= -70) return <SignalCellularAlt2BarIcon />;
    if (rssiValue >= -80) return <SignalCellularAlt1BarIcon />;
    return <SignalCellularAltIcon />;
  };

  // Get RSSI status text
  const getRSSIStatusText = (rssiValue) => {
    if (rssiValue === null || rssiValue === undefined)
      return "Connected, waiting for data...";
    return `RSSI: ${rssiValue} dBm`;
  };

  const buttonProps = {
    size: "medium",
    color: "primary",
  };

  const handleClick = () => {
    if (deviceOnline) {
      setModalOpen(true);
    }
  };

  const handleCloseModal = () => {
    setModalOpen(false);
  };

  const button = (
    <Fab
      {...buttonProps}
      onClick={deviceOnline ? handleClick : undefined}
      disabled={!deviceOnline}
      sx={{
        color: !deviceOnline
          ? "text.disabled"
          : getRSSIColor
          ? getRSSIColor(rssiValue)
          : "primary.main",
        backgroundColor: !deviceOnline ? "action.disabled" : "action.hover",
        "&:hover": {
          backgroundColor: !deviceOnline
            ? "action.disabled"
            : "action.selected",
        },
      }}
    >
      {rssiDisplayMode === "numbers" && rssiValue !== null
        ? rssiValue
        : getRSSIIconComponent(rssiValue)}
    </Fab>
  );

  // Wrap disabled buttons in a span to fix MUI Tooltip warning
  if (!deviceOnline) {
    return (
      <Tooltip title={getRSSIStatusText(rssiValue)}>
        <span>{button}</span>
      </Tooltip>
    );
  }

  return (
    <>
      <Tooltip title={getRSSIStatusText(rssiValue)}>{button}</Tooltip>

      {modalOpen && (
        <RSSISettingsModal
          config={config}
          saveConfig={saveConfig}
          saveConfigToDevice={saveConfigToDevice}
          deviceOnline={deviceOnline}
          open={modalOpen}
          onClose={handleCloseModal}
          onRSSIDataChange={(value, connected) => {
            setRssiValue(value);
            if (onRSSIDataChange) {
              onRSSIDataChange(value, connected);
            }
          }}
        />
      )}
    </>
  );
}
