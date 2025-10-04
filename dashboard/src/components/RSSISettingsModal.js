import React, { useState, useEffect, useRef } from "react";
import {
  Container,
  Fab,
  Tooltip,
  FormControl,
  FormControlLabel,
  Switch,
  RadioGroup,
  Radio,
  Typography,
  Box,
} from "@mui/material";
import {
  SignalCellularAlt,
  SignalCellular4Bar,
  SignalCellular3Bar,
  SignalCellular2Bar,
  SignalCellular1Bar,
  SignalCellular0Bar,
} from "@mui/icons-material";
import SaveIcon from "@mui/icons-material/SaveAs";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";

export default function RSSISettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
}) {
  const [isModalOpen, setIsModalOpen] = useState(false);

  // RSSI settings state (for modal editing)
  const [enabled, setEnabled] = useState(false);
  const [displayMode, setDisplayMode] = useState("both"); // "icon", "numbers", "both"

  // Actual saved RSSI settings (for button display)
  const [savedEnabled, setSavedEnabled] = useState(false);
  const [savedDisplayMode, setSavedDisplayMode] = useState("both");

  // RSSI data state
  const [rssiValue, setRssiValue] = useState(null);
  const [isConnected, setIsConnected] = useState(false);
  const [configSaved, setConfigSaved] = useState(false);
  const wsRef = useRef(null);

  useEffect(() => {
    if (config?.rssi) {
      setEnabled(config.rssi.enabled || false);
      setDisplayMode(config.rssi.displayMode || "both");
      setSavedEnabled(config.rssi.enabled || false);
      setSavedDisplayMode(config.rssi.displayMode || "both");
    }
  }, [config]);

  // WebSocket connection for RSSI data
  useEffect(() => {
    if (savedEnabled && configSaved) {
      // Add a delay to allow the backend to start the RSSI service
      const connectTimeout = setTimeout(() => {
        // Construct WebSocket URL
        let wsUrl = "/rssi";
        if (wsUrl.startsWith("/")) {
          const protocol =
            window.location.protocol === "https:" ? "wss:" : "ws:";
          const hostname = window.location.hostname;
          const port = window.location.port ? `:${window.location.port}` : "";
          wsUrl = `${protocol}//${hostname}${port}${wsUrl}`;
        }

        // Connect to RSSI WebSocket
        const ws = new WebSocket(wsUrl);
        wsRef.current = ws;

        ws.onopen = () => {
          setIsConnected(true);
        };

        ws.onmessage = (event) => {
          try {
            // RSSI data comes as plain text (just the number)
            const rssiValue = parseInt(event.data);
            if (!isNaN(rssiValue)) {
              setRssiValue(rssiValue);
            }
          } catch (error) {
            console.error("Error parsing RSSI data:", error);
          }
        };

        ws.onerror = (error) => {
          console.error("RSSI WebSocket error:", error);
          setIsConnected(false);
        };

        ws.onclose = (event) => {
          setIsConnected(false);
          wsRef.current = null;

          // Only retry if RSSI is still enabled and it's not a normal closure
          if (event.code !== 1000 && savedEnabled && configSaved) {
            setTimeout(() => {
              // Double-check that RSSI is still enabled before retrying
              if (savedEnabled && configSaved && !wsRef.current) {
                // Retry connection
                const retryWs = new WebSocket(wsUrl);
                wsRef.current = retryWs;

                retryWs.onopen = () => setIsConnected(true);
                retryWs.onmessage = (event) => {
                  try {
                    // RSSI data comes as plain text (just the number)
                    const rssiValue = parseInt(event.data);
                    if (!isNaN(rssiValue)) {
                      setRssiValue(rssiValue);
                    }
                  } catch (error) {
                    console.error("Error parsing RSSI data:", error);
                  }
                };
                retryWs.onerror = (error) => {
                  console.error("RSSI WebSocket retry error:", error);
                  setIsConnected(false);
                };
                retryWs.onclose = () => {
                  setIsConnected(false);
                  wsRef.current = null;
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
    } else {
      // Disconnect if disabled or config not saved
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
      setIsConnected(false);
      setRssiValue(null);
    }
  }, [savedEnabled, configSaved]);

  // Cleanup WebSocket on unmount
  useEffect(() => {
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current = null;
      }
    };
  }, []);

  // Get appropriate RSSI icon based on signal strength
  const getRSSIIcon = (rssi) => {
    if (rssi === null || rssi === undefined) {
      return <SignalCellularAlt />;
    }

    if (rssi >= -50) return <SignalCellular4Bar />;
    if (rssi >= -60) return <SignalCellular3Bar />;
    if (rssi >= -70) return <SignalCellular2Bar />;
    if (rssi >= -80) return <SignalCellular1Bar />;
    return <SignalCellular0Bar />;
  };

  // Get RSSI color based on signal strength
  const getRSSIColor = (rssi) => {
    if (rssi === null || rssi === undefined) {
      return "text.disabled";
    }

    if (rssi >= -50) return "primary.main";
    if (rssi >= -60) return "primary.main";
    if (rssi >= -70) return "warning.main";
    if (rssi >= -80) return "warning.main";
    return "error.main";
  };

  const handleOpenModal = () => {
    setIsModalOpen(true);
  };

  const handleCloseModal = () => {
    setIsModalOpen(false);
  };

  const handleEnabledChange = (event) => {
    setEnabled(event.target.checked);
  };

  const handleDisplayModeChange = (event) => {
    setDisplayMode(event.target.value);
  };

  const handleSave = () => {
    const configToSave = {
      ...config,
      rssi: {
        enabled: enabled,
        displayMode: displayMode,
      },
    };

    // Save to device (not just local config)
    // The backend will automatically start/stop RSSI based on enabled state
    saveConfigToDevice(configToSave);

    // Update saved settings to match what was just saved
    setSavedEnabled(enabled);
    setSavedDisplayMode(displayMode);

    // Mark config as saved so WebSocket can connect
    setConfigSaved(true);

    handleCloseModal();
  };

  return (
    <Container
      sx={{
        display: "flex",
        flexWrap: "wrap",
        justifyContent: "center",
      }}
    >
      <Tooltip
        title={
          savedEnabled
            ? configSaved
              ? `RSSI: ${
                  isConnected
                    ? rssiValue !== null
                      ? `${rssiValue} dBm`
                      : "Connected, waiting for data..."
                    : "Connecting..."
                }`
              : "RSSI - Save config to start"
            : "RSSI - Disabled"
        }
      >
        <Fab
          size="small"
          color="primary"
          aria-label="rssi-settings"
          onClick={handleOpenModal}
          sx={{
            position: "fixed",
            top: "20px",
            left: "140px", // Position next to camera button
            color: savedEnabled ? getRSSIColor(rssiValue) : "text.disabled",
            backgroundColor: savedEnabled ? "action.hover" : "action.disabled",
            "&:hover": {
              backgroundColor: savedEnabled
                ? "action.selected"
                : "action.disabledBackground",
            },
          }}
        >
          {(() => {
            if (!savedEnabled) {
              return <SignalCellularAlt />;
            }

            // Show different content based on saved display mode
            if (savedDisplayMode === "numbers") {
              return rssiValue !== null ? (
                <Typography
                  variant="caption"
                  sx={{ fontSize: "10px", fontWeight: "bold" }}
                >
                  {rssiValue}
                </Typography>
              ) : (
                <SignalCellularAlt />
              );
            } else if (savedDisplayMode === "icon") {
              return getRSSIIcon(rssiValue);
            } else {
              // "both"
              return getRSSIIcon(rssiValue);
            }
          })()}
        </Fab>
      </Tooltip>

      <SettingsModal
        open={isModalOpen}
        onClose={handleCloseModal}
        title="RSSI Settings"
        actions={
          <IButton
            color="primary"
            Icon={SaveIcon}
            onClick={handleSave}
            tooltip={"Save RSSI Settings to Device"}
          />
        }
      >
        <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
          <FormControlLabel
            control={
              <Switch
                checked={enabled}
                onChange={handleEnabledChange}
                color="primary"
              />
            }
            label="Enable RSSI Display"
          />
        </FormControl>

        {enabled && (
          <Box sx={{ marginTop: 3 }}>
            <Typography gutterBottom>Display Mode:</Typography>
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
                label="Icon Only (Signal Bars)"
              />
              <FormControlLabel
                value="numbers"
                control={<Radio />}
                label="Numbers Only (dBm)"
              />
              <FormControlLabel
                value="both"
                control={<Radio />}
                label="Both Icon and Numbers"
              />
            </RadioGroup>
          </Box>
        )}

        {enabled && (
          <Box
            sx={{
              marginTop: 2,
              padding: 2,
              backgroundColor: "rgba(71, 255, 240, 0.1)",
              borderRadius: 1,
            }}
          >
            <Typography variant="body2" color="primary.main">
              📶 RSSI display will be enabled when settings are saved. Signal
              strength will be shown in the dashboard.
            </Typography>
            <Typography
              variant="caption"
              sx={{ marginTop: 1, color: "primary.main", display: "block" }}
            >
              WebSocket URL: ws://{window.location.hostname}:
              {window.location.port || 80}/rssi
            </Typography>
          </Box>
        )}
      </SettingsModal>
    </Container>
  );
}
