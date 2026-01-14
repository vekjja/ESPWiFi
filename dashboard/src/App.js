import React, {
  useState,
  useEffect,
  useCallback,
  useRef,
  useMemo,
  Suspense,
  lazy,
} from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import Box from "@mui/material/Box";
import LinearProgress from "@mui/material/LinearProgress";
import Typography from "@mui/material/Typography";
import { getApiUrl, buildWebSocketUrl } from "./utils/apiUtils";
import { getRSSIThemeColor } from "./utils/rssiUtils";

// Lazy-load heavier UI chunks so first paint is faster
const Modules = lazy(() => import("./components/Modules"));
const Login = lazy(() => import("./components/Login"));
const SettingsButtonBar = lazy(() => import("./components/SettingsButtonBar"));

// Define the theme
const theme = createTheme({
  typography: {
    fontFamily: [
      "Roboto",
      "-apple-system",
      "BlinkMacSystemFont",
      '"Segoe UI"',
      '"Helvetica Neue"',
      "Arial",
      "sans-serif",
      '"Apple Color Emoji"',
      '"Segoe UI Emoji"',
      '"Segoe UI Symbol"',
    ].join(","),
    headerFontFamily: [
      "Roboto",
      "Source Code Pro",
      "Menlo",
      "Consolas",
      "Liberation Mono",
      "Courier New",
      "monospace",
    ].join(","),
  },
  shape: {
    borderRadius: 3,
  },
  palette: {
    mode: "dark",
    primary: {
      main: "#47FFF0",
      alt: "#FF4186",
    },
    secondary: {
      main: "#333",
    },
    error: {
      main: "#FE4245",
      alt: "#FF4186",
    },
    success: {
      main: "#17EB9D",
    },
    warning: {
      main: "#ffa726", // Default Material-UI warning color for dark mode (yellow/orange)
    },
    musicPlayer: {
      playing: "#47FFF0", // Cyan/primary - same as primary for consistency
      paused: "#ffa726", // Orange/warning - indicates temporary pause
      stopped: "#333", // Gray/secondary - neutral state
    },
  },
  components: {
    MuiTooltip: {
      styleOverrides: {
        tooltip: {
          fontSize: "0.75rem",
          backgroundColor: "primary",
          color: "white",
          padding: "6px 10px",
          borderRadius: "4px",
          maxWidth: "200px",
          textAlign: "center",
          fontFamily: "Roboto Slab, sans-serif",
        },
        arrow: {
          color: "primary",
        },
      },
      defaultProps: {
        arrow: true,
        enterDelay: 300,
        leaveDelay: 0,
        enterNextDelay: 150,
      },
    },
  },
});

function App() {
  const [config, setConfig] = useState(null);
  const [localConfig, setLocalConfig] = useState(null);
  const [saving] = useState(false);
  const [authenticated, setAuthenticated] = useState(false);
  const [checkingAuth, setCheckingAuth] = useState(false);
  const [networkEnabled, setNetworkEnabled] = useState(false);
  const [authChecked, setAuthChecked] = useState(false);
  const controlWsRef = useRef(null);
  const controlRetryRef = useRef(null);
  const logsFetchRef = useRef({
    inProgress: false,
    expectOffset: null,
    buffer: "",
    chunks: 0,
    maxBytes: 0,
    tailBytes: 0,
  });
  const [controlConnected, setControlConnected] = useState(false);
  const [cloudDeviceConfig, setCloudDeviceConfig] = useState(null);
  const [cloudDeviceInfo, setCloudDeviceInfo] = useState(null);
  const [cloudRssi, setCloudRssi] = useState(null);
  const [cloudLogs, setCloudLogs] = useState("");
  const [cloudLogsError, setCloudLogsError] = useState("");
  const heartbeatIntervalRef = useRef(null);
  const heartbeatTimeoutRef = useRef(null);

  // Music player state for banner color
  const [musicPlaybackState, setMusicPlaybackState] = useState({
    isPlaying: false,
    isPaused: false,
  });
  // Use a ref to track music state in the connection check interval
  // This avoids recreating the WebSocket when music state changes
  const musicPlaybackStateRef = useRef(musicPlaybackState);

  // Keep ref in sync with state
  useEffect(() => {
    musicPlaybackStateRef.current = musicPlaybackState;
  }, [musicPlaybackState]);

  const apiURL = getApiUrl();
  const headerRef = useRef(null);
  const pendingCommandsRef = useRef(new Map()); // Map of cmd -> {resolve, reject, timeout}

  // Measure header height and expose as CSS var to avoid brittle vh math on mobile.
  useEffect(() => {
    const el = headerRef.current;
    if (!el) return;

    const setVar = () => {
      const h = el.getBoundingClientRect?.().height || el.offsetHeight || 0;
      const px = Math.max(0, Math.round(h));
      document.documentElement.style.setProperty(
        "--app-header-height",
        `${px}px`
      );
    };

    setVar();

    let ro = null;
    if (window.ResizeObserver) {
      ro = new ResizeObserver(() => setVar());
      try {
        ro.observe(el);
      } catch {
        // ignore
      }
    }

    window.addEventListener("resize", setVar, { passive: true });
    return () => {
      window.removeEventListener("resize", setVar);
      if (ro) {
        try {
          ro.disconnect();
        } catch {
          // ignore
        }
      }
    };
  }, []);

  // Defer any network activity until the page is fully loaded.
  // This avoids "blank screen on mobile" scenarios where the app immediately
  // starts hitting device endpoints / websockets before the UI is visible.
  useEffect(() => {
    const enable = () => {
      const run = () => {
        setNetworkEnabled(true);
        setAuthChecked(true);
        setAuthenticated(true);
      };
      // Prefer idle time so render/layout settles before network work begins.
      if (window.requestIdleCallback) {
        window.requestIdleCallback(run, { timeout: 1500 });
      } else {
        setTimeout(run, 750);
      }
    };

    if (document.readyState === "complete") {
      enable();
      return;
    }

    window.addEventListener("load", enable, { once: true });
    return () => window.removeEventListener("load", enable);
  }, []);

  // Direct connection to control WebSocket
  const controlWsUrl = useMemo(() => {
    // Use local WebSocket connection
    if (process.env.REACT_APP_API_HOST) {
      return buildWebSocketUrl("/ws/control", process.env.REACT_APP_API_HOST);
    }
    return buildWebSocketUrl("/ws/control");
  }, []);

  // Once boot phase is complete, connect the control socket.
  useEffect(() => {
    if (!networkEnabled) return;
    if (!controlWsUrl) return;

    console.log(
      "[App] Setting up control WebSocket connection to:",
      controlWsUrl
    );

    const connect = () => {
      if (!networkEnabled) return;
      const uiUrl = controlWsUrl;
      if (!uiUrl) return;

      console.log("[App] Connecting control WebSocket:", uiUrl);

      // Tear down any existing socket before reconnect.
      if (controlWsRef.current) {
        try {
          controlWsRef.current.close(1000, "Reconnecting");
        } catch {
          // ignore
        }
        controlWsRef.current = null;
      }

      try {
        const ws = new WebSocket(uiUrl);
        // Needed for camera streaming over /ws/control.
        ws.binaryType = "arraybuffer";
        controlWsRef.current = ws;

        // Add a sendCommand method to the WebSocket for components to use
        ws.sendCommand = (cmd, timeoutMs = 10000) => {
          return new Promise((resolve, reject) => {
            if (ws.readyState !== WebSocket.OPEN) {
              reject(new Error("WebSocket not connected"));
              return;
            }

            const timeout = setTimeout(() => {
              pendingCommandsRef.current.delete(cmd.cmd);
              reject(new Error("Command timeout"));
            }, timeoutMs);

            pendingCommandsRef.current.set(cmd.cmd, {
              resolve,
              reject,
              timeout,
            });
            ws.send(JSON.stringify(cmd));
          });
        };

        ws.onopen = () => {
          console.log("[App] Control WebSocket connected");
          setControlConnected(true);

          // Clear any existing heartbeat
          if (heartbeatIntervalRef.current) {
            clearInterval(heartbeatIntervalRef.current);
            heartbeatIntervalRef.current = null;
          }
          if (heartbeatTimeoutRef.current) {
            clearTimeout(heartbeatTimeoutRef.current);
            heartbeatTimeoutRef.current = null;
          }

          // Start heartbeat: poll get_rssi every 3 seconds
          const startHeartbeat = () => {
            if (heartbeatIntervalRef.current) {
              clearInterval(heartbeatIntervalRef.current);
            }

            const sendHeartbeat = () => {
              const currentWs = controlWsRef.current;
              if (!currentWs || currentWs.readyState !== WebSocket.OPEN) {
                return;
              }

              try {
                // Set a timeout to detect if we don't get a response
                if (heartbeatTimeoutRef.current) {
                  clearTimeout(heartbeatTimeoutRef.current);
                }

                heartbeatTimeoutRef.current = setTimeout(() => {
                  console.warn(
                    "[App] Heartbeat timeout - no RSSI response, reconnecting..."
                  );
                  const wsToClose = controlWsRef.current;
                  if (wsToClose && wsToClose.readyState === WebSocket.OPEN) {
                    try {
                      wsToClose.close(1006, "Heartbeat timeout");
                    } catch {
                      // ignore
                    }
                  }
                }, 6000); // Allow 6 seconds for response (2x heartbeat interval)

                currentWs.send(JSON.stringify({ cmd: "get_rssi" }));
              } catch (err) {
                console.error("[App] Heartbeat send failed:", err);
                // Close connection on send failure
                try {
                  currentWs.close(1006, "Heartbeat send failed");
                } catch {
                  // ignore
                }
              }
            };

            // Send initial heartbeat immediately
            sendHeartbeat();

            // Then poll every 3 seconds
            heartbeatIntervalRef.current = setInterval(sendHeartbeat, 3000);
          };

          startHeartbeat();

          try {
            ws.send(JSON.stringify({ cmd: "ping" }));
            ws.send(JSON.stringify({ cmd: "get_config" }));
            ws.send(JSON.stringify({ cmd: "get_info" }));
          } catch {
            // ignore
          }
        };
        ws.onmessage = (evt) => {
          try {
            const msg = JSON.parse(evt?.data || "{}");

            // Check if this message is a response to a pending command
            if (msg?.cmd && pendingCommandsRef.current.has(msg.cmd)) {
              const pending = pendingCommandsRef.current.get(msg.cmd);
              clearTimeout(pending.timeout);
              pendingCommandsRef.current.delete(msg.cmd);

              if (msg.ok === false) {
                pending.reject(new Error(msg.error || "Command failed"));
              } else {
                pending.resolve(msg);
              }
              return; // Don't process further
            }

            if (msg?.cmd === "get_config" && msg?.config) {
              setConfig(msg.config);
              setLocalConfig(msg.config);
              setCloudDeviceConfig(msg.config);
            }
            if (msg?.cmd === "get_info" && msg?.info) {
              setCloudDeviceInfo(msg.info);
            }
            if (msg?.cmd === "get_rssi") {
              // Clear heartbeat timeout since we got a response
              if (heartbeatTimeoutRef.current) {
                clearTimeout(heartbeatTimeoutRef.current);
                heartbeatTimeoutRef.current = null;
              }

              if (typeof msg?.rssi === "number") {
                setCloudRssi(msg.rssi);
              } else {
                setCloudRssi(null);
              }
            }
            if (msg?.cmd === "get_logs") {
              if (msg?.ok === false) {
                setCloudLogsError(msg?.error || "get_logs_failed");
                logsFetchRef.current.inProgress = false;
                logsFetchRef.current.expectOffset = null;
                logsFetchRef.current.buffer = "";
                logsFetchRef.current.chunks = 0;
              } else {
                setCloudLogsError("");
              }
              if (typeof msg?.logs === "string") {
                const lf = logsFetchRef.current;
                if (!lf.inProgress) {
                  // Back-compat: if logs weren't requested via the chunk fetcher,
                  // still show the payload.
                  setCloudLogs(msg.logs);
                } else {
                  // Enforce sequential offsets so stale responses don't corrupt
                  // the buffer if the user re-triggers a fetch.
                  const off = Number.isFinite(msg?.offset)
                    ? Number(msg.offset)
                    : null;
                  if (lf.expectOffset !== null && off !== lf.expectOffset) {
                    return;
                  }

                  lf.buffer += msg.logs;
                  lf.chunks += 1;
                  lf.expectOffset = Number.isFinite(msg?.next)
                    ? Number(msg.next)
                    : null;

                  // Update UI every few chunks to avoid excessive rerenders.
                  if (msg?.eof || lf.chunks % 4 === 0) {
                    setCloudLogs(lf.buffer);
                  }

                  if (!msg?.eof && lf.expectOffset !== null) {
                    try {
                      ws.send(
                        JSON.stringify({
                          cmd: "get_logs",
                          offset: lf.expectOffset,
                          maxBytes: lf.maxBytes,
                          tailBytes: lf.tailBytes,
                        })
                      );
                    } catch {
                      lf.inProgress = false;
                    }
                  } else {
                    lf.inProgress = false;
                    lf.expectOffset = null;
                  }
                }
              }
            }
          } catch {
            // ignore
          }
        };
        ws.onerror = (err) => {
          console.error("[App] Control WebSocket error:", err);
          setControlConnected(false);
        };
        ws.onclose = (evt) => {
          console.log("[App] Control WebSocket closed, code:", evt?.code);

          // Clear heartbeat
          if (heartbeatIntervalRef.current) {
            clearInterval(heartbeatIntervalRef.current);
            heartbeatIntervalRef.current = null;
          }
          if (heartbeatTimeoutRef.current) {
            clearTimeout(heartbeatTimeoutRef.current);
            heartbeatTimeoutRef.current = null;
          }

          controlWsRef.current = null;
          setControlConnected(false);
          setCloudDeviceConfig(null);
          setCloudDeviceInfo(null);
          setCloudRssi(null);

          // Reject all pending commands
          for (const [, pending] of pendingCommandsRef.current.entries()) {
            clearTimeout(pending.timeout);
            pending.reject(new Error("WebSocket closed"));
          }
          pendingCommandsRef.current.clear();

          if (evt?.code === 1000) {
            console.log("[App] Clean close (1000), not reconnecting");
            return;
          }

          console.log("[App] Scheduling reconnect...");
          controlRetryRef.current = setTimeout(connect, 600);
        };
      } catch {
        setControlConnected(false);
      }
    };

    connect();
    return () => {
      // Clear heartbeat
      if (heartbeatIntervalRef.current) {
        clearInterval(heartbeatIntervalRef.current);
        heartbeatIntervalRef.current = null;
      }
      if (heartbeatTimeoutRef.current) {
        clearTimeout(heartbeatTimeoutRef.current);
        heartbeatTimeoutRef.current = null;
      }

      if (controlRetryRef.current) {
        clearTimeout(controlRetryRef.current);
        controlRetryRef.current = null;
      }
      if (controlWsRef.current) {
        try {
          controlWsRef.current.close(1000, "Cleanup");
        } catch {
          // ignore
        }
        controlWsRef.current = null;
      }
    };
  }, [networkEnabled, controlWsUrl]);

  // No HTTP config endpoint in the dashboard anymore. Config/info are sourced from
  // /ws/control only.
  useEffect(() => {
    if (!networkEnabled) return;
    setAuthChecked(true);
    setCheckingAuth(false);
    setAuthenticated(true);
  }, [networkEnabled]);

  const handleLoginSuccess = () => {
    // Legacy: keep the handler so the Login component can still resolve.
    setAuthenticated(true);
  };

  /**
   * Update local config only (no device API calls)
   * Used for immediate UI updates before saving to device
   * @param {Object} newConfig - The new configuration object (can be partial)
   */
  const updateLocalConfig = (newConfig) => {
    setLocalConfig((prevConfig) => ({ ...prevConfig, ...newConfig, apiURL }));
    // If we're operating from a tunneled device config, keep it in sync for
    // immediate UI updates.
    setCloudDeviceConfig((prev) => (prev ? { ...prev, ...newConfig } : prev));
  };

  /**
   * Save configuration to device and update local state
   * Used by buttons and components that need to persist changes
   * Includes timeout handling and offline detection
   * @param {Object} newConfig - The new configuration to save
   */
  const saveConfigFromButton = useCallback(
    (newConfig) => {
      // One mode: send config over control WS (no HTTP config endpoint).
      // Optimistically update local UI config.
      setLocalConfig((prevConfig) => ({
        ...prevConfig,
        ...newConfig,
        apiURL,
      }));
      setCloudDeviceConfig((prev) => (prev ? { ...prev, ...newConfig } : prev));

      const ws = controlWsRef.current;
      if (!ws || ws.readyState !== 1) {
        console.warn("Control WS not connected; config saved locally only.");
        return Promise.resolve(null);
      }
      try {
        ws.send(JSON.stringify({ cmd: "set_config", config: newConfig }));
        setTimeout(() => {
          try {
            ws.send(JSON.stringify({ cmd: "get_config" }));
            ws.send(JSON.stringify({ cmd: "get_info" }));
          } catch {
            // ignore
          }
        }, 750);
        return Promise.resolve(true);
      } catch (e) {
        console.warn("Failed to send set_config over control WS:", e);
        return Promise.resolve(false);
      }
    },
    [apiURL]
  );

  /**
   * Refresh config and info from device
   * Used when opening settings to get latest state
   */
  const refreshConfigFromDevice = useCallback(() => {
    const ws = controlWsRef.current;
    if (!ws || ws.readyState !== 1) {
      console.warn("Control WS not connected; cannot refresh config.");
      return;
    }
    try {
      console.log("📡 Refreshing config and info from device...");
      ws.send(JSON.stringify({ cmd: "get_config" }));
      ws.send(JSON.stringify({ cmd: "get_info" }));
    } catch (err) {
      console.error("Error refreshing config from device:", err);
    }
  }, []);

  /**
   * Get RSSI color based on signal strength
   * @param {number} rssi - The RSSI value in dBm
   * @returns {string} Theme color string
   */
  const getRSSIColor = (rssi) => getRSSIThemeColor(rssi);

  /**
   * Get appropriate RSSI icon name based on signal strength
   * @param {number|null|undefined} rssi - The RSSI value in dBm
   * @returns {string|null} Icon name or null for default
   */
  const getRSSIIcon = (rssi) => {
    if (rssi === null || rssi === undefined) return null;
    if (rssi >= -50) return "SignalCellular4Bar";
    if (rssi >= -60) return "SignalCellular3Bar";
    if (rssi >= -70) return "SignalCellular2Bar";
    if (rssi >= -80) return "SignalCellular1Bar";
    return "SignalCellular0Bar";
  };

  const effectiveConfig = cloudDeviceConfig || localConfig;

  return (
    <ThemeProvider theme={theme}>
      {/* Show saving progress bar at the top when saving */}
      {saving && (
        <LinearProgress
          color="primary"
          sx={{
            position: "fixed",
            top: 0,
            left: 0,
            right: 0,
            zIndex: 1002,
          }}
        />
      )}

      {/* Show connection progress bar while we're determining auth/config */}
      {checkingAuth && !saving && (
        <LinearProgress
          color="inherit"
          sx={{
            position: "fixed",
            top: 0,
            left: 0,
            right: 0,
            zIndex: 1002,
          }}
        />
      )}

      <Container
        ref={headerRef}
        sx={{
          fontFamily: theme.typography.headerFontFamily,
          backgroundColor:
            musicPlaybackState.isPlaying && musicPlaybackState.isPaused
              ? "musicPlayer.paused"
              : musicPlaybackState.isPlaying
              ? "musicPlayer.playing"
              : controlConnected
              ? "secondary.light"
              : "error.main",
          color:
            musicPlaybackState.isPlaying || !controlConnected
              ? "white"
              : "primary.main",
          fontSize: "3em",
          // Prefer measured height (CSS var) but keep a fallback for first paint.
          height: "var(--app-header-height, 9vh)",
          zIndex: 1000,
          textAlign: "center",
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          minWidth: "100%",
          position: "sticky",
          top: 0,
          transition: "background-color 0.3s ease",
        }}
      >
        <Box sx={{ display: "flex", alignItems: "center", gap: 2 }}>
          {localConfig?.["deviceName"] || config?.["deviceName"] || "ESPWiFi"}
          <Typography
            variant="caption"
            sx={{ opacity: 0.75, fontFamily: "inherit" }}
          >
            {musicPlaybackState.isPlaying
              ? musicPlaybackState.isPaused
                ? "Music: paused"
                : "Music: playing"
              : `Control: ${controlConnected ? "connected" : "disconnected"}`}
          </Typography>
        </Box>
      </Container>

      {/* Settings Button Bar */}
      <Suspense fallback={null}>
        <SettingsButtonBar
          config={effectiveConfig}
          deviceOnline={controlConnected}
          saveConfig={updateLocalConfig}
          saveConfigToDevice={saveConfigFromButton}
          onRefreshConfig={refreshConfigFromDevice}
          getRSSIColor={getRSSIColor}
          getRSSIIcon={getRSSIIcon}
          controlRssi={cloudRssi}
          logsText={cloudLogs}
          logsError={cloudLogsError}
          onRequestLogs={() => {
            const ws = controlWsRef.current;
            if (!ws || ws.readyState !== 1) return;
            try {
              const lf = logsFetchRef.current;
              if (lf.inProgress) return;

              const tailBytes = 256 * 1024;
              const maxBytes = 8 * 1024;

              lf.inProgress = true;
              lf.expectOffset = null;
              lf.buffer = "";
              lf.chunks = 0;
              lf.maxBytes = maxBytes;
              lf.tailBytes = tailBytes;

              setCloudLogs("");
              setCloudLogsError("");

              ws.send(JSON.stringify({ cmd: "get_logs", tailBytes, maxBytes }));
            } catch {
              // ignore
            }
          }}
          cloudMode={false}
          controlConnected={controlConnected}
          deviceInfoOverride={cloudDeviceInfo}
          controlWs={controlWsRef.current}
          musicPlaybackState={musicPlaybackState}
        />
      </Suspense>

      <Container>
        <Suspense fallback={<LinearProgress color="inherit" />}>
          <Modules
            config={effectiveConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={controlConnected}
            controlWs={controlWsRef.current}
            onMusicPlaybackChange={setMusicPlaybackState}
          />
        </Suspense>
      </Container>

      {/* Show login modal when not authenticated */}
      {!authenticated && authChecked && !checkingAuth && (
        <Suspense fallback={null}>
          <Login onLoginSuccess={handleLoginSuccess} />
        </Suspense>
      )}
    </ThemeProvider>
  );
}

export default App;
