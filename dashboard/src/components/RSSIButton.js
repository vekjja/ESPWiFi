import React, { useState, useEffect, useRef } from "react";
import { Fab, Tooltip } from "@mui/material";
import {
  SignalCellularAlt as SignalCellularAltIcon,
  SignalCellularAlt1Bar as SignalCellularAlt1BarIcon,
  SignalCellularAlt2Bar as SignalCellularAlt2BarIcon,
} from "@mui/icons-material";
import RSSISettingsModal from "./RSSISettingsModal";
import { buildWebSocketUrl } from "../utils/apiUtils";

export default function RSSIButton({
  config,
  deviceOnline,
  saveConfig,
  saveConfigToDevice,
  onRSSIDataChange,
  getRSSIColor,
}) {
  const [modalOpen, setModalOpen] = useState(false);
  const [rssiValue, setRssiValue] = useState(null);
  const wsRef = useRef(null);
  const connectTimeoutRef = useRef(null);
  const retryTimeoutRef = useRef(null);
  const deviceOnlineRef = useRef(deviceOnline);
  const shouldReconnectRef = useRef(true);

  // WebSocket connection for RSSI data - always connect when device is online
  useEffect(() => {
    deviceOnlineRef.current = deviceOnline;
  }, [deviceOnline]);

  // Stop all reconnect attempts when the component unmounts (e.g. button removed)
  useEffect(() => {
    return () => {
      shouldReconnectRef.current = false;
      if (connectTimeoutRef.current) {
        clearTimeout(connectTimeoutRef.current);
        connectTimeoutRef.current = null;
      }
      if (retryTimeoutRef.current) {
        clearTimeout(retryTimeoutRef.current);
        retryTimeoutRef.current = null;
      }
      if (wsRef.current) {
        try {
          wsRef.current.close(1000, "RSSI button removed");
        } catch {
          // ignore
        }
        wsRef.current = null;
      }
    };
  }, []);

  // WebSocket connection for RSSI data - always connect when device is online
  useEffect(() => {
    if (deviceOnline) {
      // Add a delay to allow the backend to start the RSSI service
      connectTimeoutRef.current = setTimeout(() => {
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

          // Only retry if device is still online
          if (
            shouldReconnectRef.current &&
            event.code !== 1000 &&
            deviceOnlineRef.current
          ) {
            retryTimeoutRef.current = setTimeout(() => {
              // Double-check that device is still online before retrying
              if (
                shouldReconnectRef.current &&
                deviceOnlineRef.current &&
                !wsRef.current
              ) {
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
      }, 300); // 300ms delay

      return () => {
        if (connectTimeoutRef.current) {
          clearTimeout(connectTimeoutRef.current);
          connectTimeoutRef.current = null;
        }
        if (retryTimeoutRef.current) {
          clearTimeout(retryTimeoutRef.current);
          retryTimeoutRef.current = null;
        }
        if (wsRef.current) {
          wsRef.current.close(1000, "RSSI effect cleanup");
          wsRef.current = null;
        }
      };
    } else if (!deviceOnline) {
      // Disconnect if device offline
      if (retryTimeoutRef.current) {
        clearTimeout(retryTimeoutRef.current);
        retryTimeoutRef.current = null;
      }
      if (wsRef.current) {
        wsRef.current.close(1000, "Device offline");
        wsRef.current = null;
      }
      setRssiValue(null);
    }
  }, [deviceOnline, config?.deviceName]);

  // Cleanup WebSocket on unmount
  useEffect(() => {
    return () => {
      if (wsRef.current) {
        wsRef.current.close(1000, "RSSI unmount");
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
      {getRSSIIconComponent(rssiValue)}
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
