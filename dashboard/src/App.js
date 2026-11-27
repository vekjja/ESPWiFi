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
import LinearProgress from "@mui/material/LinearProgress";
import { getApiUrl, buildWebSocketUrl } from "./utils/api";
import { getRSSIThemeColor } from "./utils/rssi";
import { getAuthToken, clearAuthToken } from "./utils/auth";
import { createSocketSession } from "./utils/socketSession";
import {
  configForDevice,
  rememberBrowserModules,
  withBrowserModules,
} from "./components/modules/moduleLayout";

// espwifi.local resolves over mDNS. The IPv4 answer is immediate, but the
// IPv6 query sits until it times out (~5s). Chrome's WebSocket uses that
// full lookup, so a short connect timeout aborts every attempt before the
// socket can open. Once the device tells us its address, later attempts
// skip the name.
const DEVICE_ADDRESS_KEY = "espwifi.deviceAddress";
const NAME_CONNECT_TIMEOUT_MS = 8000;
const ADDRESS_CONNECT_TIMEOUT_MS = 2000;

function isDeviceIpv4(value) {
  const match = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(value || "");
  if (!match) return false;
  if (value === "0.0.0.0") return false;
  return match.slice(1).every((part) => Number(part) <= 255);
}

function readDeviceAddress() {
  try {
    const saved = sessionStorage.getItem(DEVICE_ADDRESS_KEY);
    return isDeviceIpv4(saved) ? saved : "";
  } catch {
    return "";
  }
}

function writeDeviceAddress(address) {
  try {
    if (address) sessionStorage.setItem(DEVICE_ADDRESS_KEY, address);
    else sessionStorage.removeItem(DEVICE_ADDRESS_KEY);
  } catch {
    // Private mode can reject storage. The in-memory address still works.
  }
}

function socketUrlForAddress(url, address) {
  if (!url || !isDeviceIpv4(address)) return url;
  const parsed = new URL(url);
  parsed.hostname = address;
  return parsed.toString();
}

// Lazy-load heavier UI chunks so first paint is faster
const Modules = lazy(() => import("./components/modules/Modules"));
const Login = lazy(() => import("./components/auth/Login"));
const SettingsButtonBar = lazy(() => import("./components/toolbar/SettingsButtonBar"));

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
      playing: "#8400FF",
      paused: "#ffa726",
      stopped: "#333",
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
        placement: "bottom",
        // The popper sits on top of the anchor and steals the click. Keep the
        // label below the button, and let clicks pass through the popup.
        disableInteractive: true,
        enterDelay: 300,
        leaveDelay: 0,
        enterNextDelay: 150,
        slotProps: {
          popper: {
            popperOptions: {
              modifiers: [
                {
                  name: "offset",
                  options: { offset: [0, 9] },
                },
              ],
            },
          },
        },
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
  const controlSessionRef = useRef(null);
  const logsFetchRef = useRef({
    inProgress: false,
    expectOffset: null,
    buffer: "",
    chunks: 0,
    maxBytes: 0,
    tailBytes: 0,
  });
  const [controlStatus, setControlStatus] = useState("disconnected");
  const controlConnected = controlStatus === "connected";
  const [cloudDeviceConfig, setCloudDeviceConfig] = useState(null);
  const [cloudDeviceInfo, setCloudDeviceInfo] = useState(null);
  const [cloudRssi, setCloudRssi] = useState(null);
  const [cloudLogs, setCloudLogs] = useState("");
  const [cloudLogsError, setCloudLogsError] = useState("");
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
  // Replies are matched by command name, so only one command can be in
  // flight. Music and movies both list files on connect; queue them.
  const commandQueueRef = useRef(Promise.resolve());

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

  // Determine whether auth is required (and whether we already have a token).
  // We intentionally probe /api/auth/login since it is excluded from auth.
  useEffect(() => {
    if (!networkEnabled) return;

    let cancelled = false;

    const bootstrapAuth = async () => {
      setCheckingAuth(true);
      try {
        const existing = getAuthToken();
        if (existing && existing !== "null" && existing !== "undefined") {
          if (!cancelled) {
            setAuthenticated(true);
            setAuthChecked(true);
            setCheckingAuth(false);
          }
          return;
        }

        // Probe: if auth is disabled, this returns 200 with token:""
        // If auth is enabled, this returns 401.
        const resp = await fetch(`${apiURL}/api/auth/login`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ username: "", password: "" }),
        });

        if (cancelled) return;

        if (resp.ok) {
          // Auth disabled → allow access without storing a token.
          setAuthenticated(true);
        } else if (resp.status === 401) {
          // Auth enabled → require Login modal.
          setAuthenticated(false);
        } else {
          // Unexpected response; don't hard-block the UI.
          setAuthenticated(true);
        }
        setAuthChecked(true);
        setCheckingAuth(false);
      } catch (err) {
        // If we're offline / device not reachable, don't trap the UI in login.
        if (!cancelled) {
          console.warn("[App] Auth bootstrap failed:", err);
          setAuthenticated(true);
          setAuthChecked(true);
          setCheckingAuth(false);
        }
      }
    };

    bootstrapAuth();
    return () => {
      cancelled = true;
    };
  }, [networkEnabled, apiURL]);

  // Direct connection to control WebSocket
  const controlWsUrl = useMemo(() => {
    // Use local WebSocket connection
    if (process.env.REACT_APP_API_HOST) {
      return buildWebSocketUrl("/ws/control", process.env.REACT_APP_API_HOST);
    }
    return buildWebSocketUrl("/ws/control");
    // Recompute when auth state changes so the URL picks up ?token=...
    // (buildWebSocketUrl reads the token from storage).
  }, [authenticated]);

  // Once boot phase is complete, keep the control socket up.
  // Reconnects use backoff, pause while the browser is offline, and retry
  // as soon as the network or the tab comes back. A bad token (close 1008)
  // stops the loop and asks for login. A dropped network (1006) does not.
  useEffect(() => {
    if (!networkEnabled) return;
    if (!authenticated) return;
    if (!controlWsUrl) return;

    const rejectPending = (reason) => {
      for (const [, pending] of pendingCommandsRef.current.entries()) {
        clearTimeout(pending.timeout);
        pending.reject(new Error(reason));
      }
      pendingCommandsRef.current.clear();
    };

    const handleControlMessage = (evt, ws) => {
      let msg;
      try {
        msg = JSON.parse(evt?.data || "{}");
      } catch {
        return;
      }

      if (msg?.cmd === "get_config" && msg?.config) {
        const nextConfig = withBrowserModules(msg.config);
        setConfig(nextConfig);
        setLocalConfig(nextConfig);
        setCloudDeviceConfig(nextConfig);
      }
      if (msg?.cmd === "get_info" && msg?.info) {
        setCloudDeviceInfo(msg.info);
        if (isDeviceIpv4(msg.info.ip)) rememberAddress(msg.info.ip);
      }
      if (msg?.cmd === "get_rssi") {
        if (typeof msg?.rssi === "number") {
          setCloudRssi(msg.rssi);
        } else {
          setCloudRssi(null);
        }
      }

      if (msg?.cmd && pendingCommandsRef.current.has(msg.cmd)) {
        const pending = pendingCommandsRef.current.get(msg.cmd);
        clearTimeout(pending.timeout);
        pendingCommandsRef.current.delete(msg.cmd);
        if (msg.ok === false) {
          pending.reject(new Error(msg.error || "Command failed"));
        } else {
          pending.resolve(msg);
        }
        return;
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
            setCloudLogs(msg.logs);
          } else {
            const off = Number.isFinite(msg?.offset) ? Number(msg.offset) : null;
            if (lf.expectOffset !== null && off !== lf.expectOffset) {
              return;
            }

            lf.buffer += msg.logs;
            lf.chunks += 1;
            lf.expectOffset = Number.isFinite(msg?.next) ? Number(msg.next) : null;

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
    };

    let infoBeat = 0;
    let deviceAddress = readDeviceAddress();
    let usedDeviceAddress = false;
    let opened = false;

    const rememberAddress = (ip) => {
      if (!isDeviceIpv4(ip) || ip === deviceAddress) return;
      deviceAddress = ip;
      writeDeviceAddress(ip);
    };

    const forgetAddress = () => {
      deviceAddress = "";
      usedDeviceAddress = false;
      writeDeviceAddress("");
    };

    console.log(
      "[App] Setting up control WebSocket connection to:",
      controlWsUrl
    );

    const session = createSocketSession({
      getUrl: () => {
        // A saved address that never opened is stale. Fall back to the name.
        if (usedDeviceAddress && !opened) forgetAddress();
        opened = false;
        if (deviceAddress) {
          usedDeviceAddress = true;
          return socketUrlForAddress(controlWsUrl, deviceAddress);
        }
        usedDeviceAddress = false;
        return controlWsUrl;
      },
      binary: true,
      heartbeatMs: 1000,
      // A busy stream or upload can delay a reply. Stay connected through
      // three missed heartbeats, then drop without waiting on the browser
      // to finish closing a dead socket.
      heartbeatTimeoutMs: 1500,
      maxMissedHeartbeats: 3,
      // The name lookup stalls on IPv6 for about five seconds. An address
      // we already learned connects immediately, so that attempt can fail fast.
      connectTimeoutMs: () =>
        deviceAddress ? ADDRESS_CONNECT_TIMEOUT_MS : NAME_CONNECT_TIMEOUT_MS,
      heartbeat: () => {
        infoBeat += 1;
        // Memory and storage change while the page is open. Refresh device
        // info on every other heartbeat so those modules stay current.
        if (infoBeat % 2 === 0) {
          const socket = controlWsRef.current;
          if (socket && socket.readyState === WebSocket.OPEN) {
            try {
              socket.send(JSON.stringify({ cmd: "get_info" }));
            } catch {
              // ignore
            }
          }
        }
        return JSON.stringify({ cmd: "get_rssi" });
      },
      onSocket: (ws) => {
        ws.sendCommand = (cmd, timeoutMs = 10000) => {
          const run = () =>
            new Promise((resolve, reject) => {
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
              try {
                ws.send(JSON.stringify(cmd));
              } catch (error) {
                clearTimeout(timeout);
                pendingCommandsRef.current.delete(cmd.cmd);
                reject(error);
              }
            });
          const queued = commandQueueRef.current.then(run, run);
          commandQueueRef.current = queued.then(
            () => {},
            () => {}
          );
          return queued;
        };
        controlWsRef.current = ws;
      },
      onStatus: (status) => {
        const down =
          status === "disconnected" ||
          status === "offline" ||
          status === "error" ||
          status === "auth";
        if (down) {
          controlWsRef.current = null;
          setCloudRssi(null);
        }
        setControlStatus(status);
      },
      onOpen: (ws) => {
        opened = true;
        console.log("[App] Control WebSocket connected");
        try {
          ws.send(JSON.stringify({ cmd: "ping" }));
          ws.send(JSON.stringify({ cmd: "get_config" }));
          ws.send(JSON.stringify({ cmd: "get_info" }));
        } catch {
          // ignore
        }
      },
      onMessage: handleControlMessage,
      onClose: (evt) => {
        console.log("[App] Control WebSocket closed, code:", evt?.code);
        controlWsRef.current = null;
        logsFetchRef.current.inProgress = false;
        rejectPending("WebSocket closed");
      },
      onAuthFailure: () => {
        console.warn(
          "[App] Control socket rejected the token. Requiring login."
        );
        clearAuthToken();
        setAuthenticated(false);
        setAuthChecked(true);
      },
    });

    controlSessionRef.current = session;
    session.start();

    return () => {
      session.dispose();
      if (controlSessionRef.current === session) {
        controlSessionRef.current = null;
      }
      controlWsRef.current = null;
      rejectPending("WebSocket closed");
    };
  }, [networkEnabled, authenticated, controlWsUrl]);

  const handleLoginSuccess = () => {
    // Login component stores the token; now allow WS connection.
    setAuthenticated(true);
    setAuthChecked(true);
    setCheckingAuth(false);
  };

  /**
   * Update local config only (no device API calls)
   * Used for immediate UI updates before saving to device
   * @param {Object} newConfig - The new configuration object (can be partial)
   */
  const updateLocalConfig = (newConfig) => {
    if (Array.isArray(newConfig?.modules)) {
      rememberBrowserModules(newConfig.modules);
    }
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
      // Modules stay in this browser, so a modules-only save is not sent.
      if (Array.isArray(newConfig?.modules)) {
        rememberBrowserModules(newConfig.modules);
      }
      const forDevice = configForDevice(newConfig);
      setLocalConfig((prevConfig) => ({
        ...prevConfig,
        ...newConfig,
        apiURL,
      }));
      setCloudDeviceConfig((prev) => (prev ? { ...prev, ...newConfig } : prev));

      if (!forDevice || Object.keys(forDevice).length === 0) {
        return Promise.resolve(true);
      }

      const ws = controlWsRef.current;
      if (!ws || ws.readyState !== 1) {
        console.warn("Control WS not connected; config saved locally only.");
        return Promise.resolve(null);
      }
      try {
        ws.send(JSON.stringify({ cmd: "set_config", config: forDevice }));
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
          backgroundColor: !controlConnected
            ? "error.main"
            : musicPlaybackState.isPlaying && musicPlaybackState.isPaused
            ? "musicPlayer.paused"
            : musicPlaybackState.isPlaying
            ? "musicPlayer.playing"
            : "secondary.light",
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
        {localConfig?.["deviceName"] || config?.["deviceName"] || "espWiFi"}
      </Container>

      {/* Settings Button Bar */}
      <Suspense fallback={null}>
        <SettingsButtonBar
          config={effectiveConfig}
          deviceOnline={controlConnected}
          controlStatus={controlStatus}
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
          controlConnected={controlConnected}
          deviceInfoOverride={cloudDeviceInfo}
          controlWs={controlWsRef.current}
        />
      </Suspense>

      <Container maxWidth={false} disableGutters sx={{ width: "100%" }}>
        <Suspense fallback={<LinearProgress color="inherit" />}>
          <Modules
            config={effectiveConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={controlConnected}
            deviceInfo={cloudDeviceInfo}
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
