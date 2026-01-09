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
import {
  getApiUrl,
  buildApiUrl,
  getFetchOptions,
  buildWebSocketUrl,
} from "./utils/apiUtils";
import { isAuthenticated, clearAuthToken } from "./utils/authUtils";
import { getRSSIThemeColor } from "./utils/rssiUtils";
import { getUserFriendlyErrorMessage } from "./utils/errorUtils";
// NOTE: control WS URL is constructed from the selected device record to avoid
// reconnect loops on config changes in paired/tunnel mode.
import {
  loadDevices,
  loadSelectedDeviceId,
  upsertDevice,
  removeDevice,
  saveDevices,
  saveSelectedDeviceId,
  touchSelected,
} from "./utils/deviceRegistry";

// Lazy-load heavier UI chunks so first paint is faster
const Modules = lazy(() => import("./components/Modules"));
const Login = lazy(() => import("./components/Login"));
const SettingsButtonBar = lazy(() => import("./components/SettingsButtonBar"));
const BlePairingDialog = lazy(() => import("./components/BlePairingDialog"));

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
  const [deviceOnline, setDeviceOnline] = useState(true);
  const [authenticated, setAuthenticated] = useState(false);
  const [checkingAuth, setCheckingAuth] = useState(false);
  const [networkEnabled, setNetworkEnabled] = useState(false);
  const [authChecked, setAuthChecked] = useState(false);
  const [deviceApiBlocked, setDeviceApiBlocked] = useState(false);
  const [deviceApiBlockedUrl, setDeviceApiBlockedUrl] = useState("");
  const [needsPairing, setNeedsPairing] = useState(false);
  const [devices, setDevices] = useState([]);
  const [selectedDeviceId, setSelectedDeviceId] = useState(null);
  const [pairingOpen, setPairingOpen] = useState(false);
  const controlWsRef = useRef(null);
  const controlRetryRef = useRef(null);
  const [controlConnected, setControlConnected] = useState(false);
  const [cloudDeviceConfig, setCloudDeviceConfig] = useState(null);
  const [cloudDeviceInfo, setCloudDeviceInfo] = useState(null);
  const [cloudRssi, setCloudRssi] = useState(null);
  const [cloudLogs, setCloudLogs] = useState("");
  const [cloudLogsError, setCloudLogsError] = useState("");

  const apiURL = getApiUrl();
  const headerRef = useRef(null);

  // Use ref for fetch locking to avoid race conditions with async state updates
  const isFetchingConfigRef = useRef(false);
  const deviceOnlineRef = useRef(deviceOnline);

  useEffect(() => {
    deviceOnlineRef.current = deviceOnline;
  }, [deviceOnline]);

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

  const isCloudDashboard =
    process.env.NODE_ENV === "production" && !process.env.REACT_APP_API_HOST;

  // Note: localhost can still operate fully via /ws/control (LAN mode), or via
  // tunnel if a device record is selected that contains tunnel details.

  // One mode: always talk over /ws/control.
  // The only difference between "LAN" and "paired/tunnel" is the control WS URL:
  // - LAN: ws(s)://<device>/ws/control
  // - Tunnel: wss://tnl.../ws/ui/<id>?tunnel=ws_control&token=...

  // Tunnel-ready means we have enough info to operate without LAN HTTP.
  const selectedDevice = useMemo(() => {
    const id = String(selectedDeviceId || "");
    if (!id) return null;
    return devices.find((d) => String(d?.id) === id) || null;
  }, [devices, selectedDeviceId]);

  // NOTE: We intentionally compute these without referencing makeCloudConfigFromDevice
  // (defined later) to keep ESLint happy and to avoid reconnecting control WS
  // when config objects change.
  const tunnelReady = Boolean(
    selectedDevice &&
      (selectedDevice.authToken || "").length > 0 &&
      (selectedDevice.cloudBaseUrl || "").length > 0 &&
      (selectedDevice.hostname || selectedDevice.id)
  );

  const controlUiUrl = useMemo(() => {
    if (!tunnelReady || !selectedDevice) return null;
    const base = selectedDevice.cloudBaseUrl || "https://tnl.espwifi.io";
    const id = selectedDevice.hostname || selectedDevice.id;
    const token = selectedDevice.authToken || "";
    if (!id || !token) return null;
    // Always use wss on https pages
    const wsProto = window.location.protocol === "https:" ? "wss:" : "ws:";
    const baseUrl =
      base.startsWith("ws:") || base.startsWith("wss:")
        ? base
        : base.replace(/^https?:/i, wsProto);
    const sep = baseUrl.endsWith("/") ? "" : "/";
    return `${baseUrl}${sep}ws/ui/${encodeURIComponent(
      id
    )}?tunnel=ws_control&token=${encodeURIComponent(token)}`;
  }, [tunnelReady, selectedDevice]);

  const [controlReconnectSeq, setControlReconnectSeq] = useState(0);

  const makeCloudConfigFromDevice = useCallback((d) => {
    if (!d) return null;
    const deviceId = d.deviceId || d.hostname || d.id;
    return {
      deviceName: d.name || d.id,
      hostname: d.hostname || d.id,
      auth: { token: d.authToken || "" },
      wifi: { mode: "client", enabled: true },
      cloudTunnel: {
        enabled: true,
        baseUrl: d.cloudBaseUrl || "https://tnl.espwifi.io",
        tunnelAll: true,
        uris: ["ws_camera", "ws_rssi", "ws_control"],
      },
      // Keep Modules/Settings bar happy
      pins: [],
      webSockets: [],
      modules: [],
      camera: { installed: true, enabled: true },
      bluetooth: { installed: true },
      deviceId,
    };
  }, []);

  // Bootstrap device registry early (before any network calls).
  useEffect(() => {
    const loaded = loadDevices();
    const sel = loadSelectedDeviceId();
    setDevices(loaded);
    setSelectedDeviceId(sel);

    const chosen =
      loaded.find((d) => String(d?.id) === String(sel)) || loaded[0] || null;

    // If we have saved devices, default-select the last selected (or first)
    // so LAN dev has a deterministic target, but still show "Select a device"
    // if nothing is selected (user can unselect via UI in the future).
    if (chosen) {
      const chosenId = String(chosen.id);
      setSelectedDeviceId(chosenId);
      saveSelectedDeviceId(chosenId);
    }

    // Show "Select a device" banner when there are saved devices but none selected,
    // and "No device paired" when there are none. This applies on both local + cloud.
    setNeedsPairing(loaded.length === 0 || !chosen);
    setAuthChecked(true);

    // If we're cloud-hosted and have a chosen device, build the virtual config.
    if (isCloudDashboard && chosen) {
      const cc = makeCloudConfigFromDevice(chosen);
      setConfig(cc);
      setLocalConfig(cc);
      setAuthenticated(true);
      // Defer marking "online" until boot phase completes so we don't start
      // websocket connects before the page is fully loaded.
      setDeviceOnline(false);
    }
  }, [isCloudDashboard, makeCloudConfigFromDevice]);

  const handleSelectDevice = useCallback(
    (d) => {
      if (!d?.id) return;
      const id = String(d.id);
      setSelectedDeviceId(id);
      saveSelectedDeviceId(id);
      // Even if selecting the same device again, force a control reconnect.
      setControlReconnectSeq((n) => n + 1);

      // Touch + persist registry
      setDevices((prev) => {
        const next = touchSelected(prev, id);
        saveDevices(next);
        return next;
      });

      if (isCloudDashboard) {
        const cc = makeCloudConfigFromDevice(d);
        setConfig(cc);
        setLocalConfig(cc);
        setNeedsPairing(false);
        setAuthenticated(true);
        setAuthChecked(true);
        // Don't connect sockets until boot phase (networkEnabled).
        setDeviceOnline(false);
      }
    },
    [isCloudDashboard, makeCloudConfigFromDevice]
  );

  const handleRemoveDevice = useCallback(
    (d) => {
      const id = String(d?.id || "");
      if (!id) return;
      setDevices((prev) => {
        const next = removeDevice(prev, id);
        saveDevices(next);
        return next;
      });
      if (String(selectedDeviceId) === id) {
        setSelectedDeviceId(null);
        saveSelectedDeviceId(null);
        setNeedsPairing(isCloudDashboard);
        if (isCloudDashboard) {
          setConfig(null);
          setLocalConfig(null);
        }
      }
    },
    [isCloudDashboard, selectedDeviceId]
  );

  const handlePairNewDevice = useCallback(() => {
    // Web Bluetooth requires a user gesture; this is triggered from the Devices dialog.
    setPairingOpen(true);
  }, []);

  // Defer any network activity until the page is fully loaded.
  // This avoids "blank screen on mobile" scenarios where the app immediately
  // starts hitting device endpoints / websockets before the UI is visible.
  useEffect(() => {
    const enable = () => {
      const run = () => setNetworkEnabled(true);
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

  const lanTargetHost = useMemo(() => {
    // Prefer explicit override.
    if (process.env.REACT_APP_API_HOST) return process.env.REACT_APP_API_HOST;
    // If a device is selected, use it as the LAN target.
    const selHost =
      selectedDevice?.hostname ||
      selectedDevice?.deviceId ||
      selectedDevice?.id;
    if (selHost) return selHost;
    // If we already have a config, prefer its hostname.
    if (localConfig?.hostname) return localConfig.hostname;
    if (config?.hostname) return config.hostname;
    // No selection yet.
    return null;
  }, [config?.hostname, localConfig?.hostname, selectedDevice]);

  const lanControlUrl = useMemo(() => {
    // If no target host yet, don't connect.
    if (!lanTargetHost) return null;
    return buildWebSocketUrl("/ws/control", lanTargetHost);
  }, [lanTargetHost]);

  const controlWsUrl = useMemo(() => {
    // Tunnel when a selected device provides tunnel connection details.
    if (tunnelReady) return controlUiUrl || null;
    // Otherwise, always use LAN control WS.
    return lanControlUrl || null;
  }, [tunnelReady, controlUiUrl, lanControlUrl]);

  // Once boot phase is complete, connect the control socket.
  useEffect(() => {
    if (!networkEnabled) return;

    // On espwifi.io, we require a selected tunneled device.
    if (isCloudDashboard && !controlWsUrl) {
      setDeviceOnline(false);
      setControlConnected(false);
      if (controlRetryRef.current) {
        clearTimeout(controlRetryRef.current);
        controlRetryRef.current = null;
      }
      if (controlWsRef.current) {
        try {
          controlWsRef.current.close(1000, "No device selected");
        } catch {
          // ignore
        }
        controlWsRef.current = null;
      }
      return;
    }

    setDeviceOnline(true);

    const connect = () => {
      if (!networkEnabled) return;
      const uiUrl = controlWsUrl;
      if (!uiUrl) return;

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
        controlWsRef.current = ws;

        ws.onopen = () => {
          setControlConnected(true);
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
            if (msg?.cmd === "get_config" && msg?.config) {
              setConfig(msg.config);
              setLocalConfig(msg.config);
              setCloudDeviceConfig(msg.config);
            }
            if (msg?.cmd === "get_info" && msg?.info) {
              setCloudDeviceInfo(msg.info);
            }
            if (msg?.cmd === "get_rssi") {
              if (typeof msg?.rssi === "number") {
                setCloudRssi(msg.rssi);
              } else {
                setCloudRssi(null);
              }
            }
            if (msg?.cmd === "get_logs") {
              if (msg?.ok === false) {
                setCloudLogsError(msg?.error || "get_logs_failed");
              } else {
                setCloudLogsError("");
              }
              if (typeof msg?.logs === "string") {
                setCloudLogs(msg.logs);
              }
            }
          } catch {
            // ignore
          }
        };
        ws.onerror = () => {
          setControlConnected(false);
          console.warn("Control WS error:", uiUrl);
        };
        ws.onclose = (evt) => {
          controlWsRef.current = null;
          setControlConnected(false);
          setCloudDeviceConfig(null);
          setCloudDeviceInfo(null);
          console.warn(
            "Control WS closed:",
            { code: evt?.code, reason: evt?.reason },
            uiUrl
          );
          if (evt?.code === 1000) return;
          controlRetryRef.current = setTimeout(connect, 2000);
        };
      } catch {
        setControlConnected(false);
      }
    };

    connect();
    return () => {
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
  }, [
    networkEnabled,
    isCloudDashboard,
    controlWsUrl,
    selectedDeviceId,
    controlReconnectSeq,
  ]);

  /**
   * Helper to compare two config objects for equality
   * @param {Object} config1 - First config object
   * @param {Object} config2 - Second config object
   * @returns {boolean} True if configs are equal
   */
  const configsAreEqual = useCallback((config1, config2) => {
    return JSON.stringify(config1) === JSON.stringify(config2);
  }, []);

  /**
   * Fetch configuration from device
   * Includes retry logic and offline detection
   * @param {boolean} forceUpdate - Force update even if config unchanged
   * @returns {Promise<Object|null>} Configuration object or null on error
   */
  const fetchConfig = useCallback(
    async (forceUpdate = false) => {
      // Prevent concurrent fetches using ref for synchronous check
      if (isFetchingConfigRef.current) {
        return null;
      }

      isFetchingConfigRef.current = true;

      try {
        // If we have a control channel URL, config/info comes from /ws/control.
        if (controlWsUrl) {
          return null;
        }
        const configUrl = buildApiUrl("/api/config");

        // If we're in production and buildApiUrl() is same-origin, we are *not*
        // pointing at a device. (espwifi.io serves only the static dashboard.)
        // Don't attempt to parse HTML as JSON; instead mark "needs pairing".
        if (
          process.env.NODE_ENV === "production" &&
          typeof configUrl === "string" &&
          (configUrl === "/api/config" ||
            configUrl === `${window.location.origin}/api/config`)
        ) {
          setNeedsPairing(true);
          return null;
        } else {
          setNeedsPairing(false);
        }
        // On https pages, browsers typically block http:// device API calls as mixed content.
        // Desktop users can sometimes override this; mobile generally cannot.
        if (
          window.location.protocol === "https:" &&
          typeof configUrl === "string" &&
          configUrl.startsWith("http://")
        ) {
          setDeviceApiBlocked(true);
          setDeviceApiBlockedUrl(configUrl);
          setDeviceOnline(false);
          setAuthenticated(false);
          return null;
        } else {
          setDeviceApiBlocked(false);
          setDeviceApiBlockedUrl("");
        }

        // Create an AbortController for timeout
        const controller = new AbortController();
        // Timeout set to 20 seconds to handle slow device responses
        const timeoutId = setTimeout(() => controller.abort(), 20000);

        const response = await fetch(
          configUrl,
          getFetchOptions({
            signal: controller.signal,
          })
        );

        clearTimeout(timeoutId);

        // Handle 401 Unauthorized - user needs to login
        if (response.status === 401) {
          setAuthenticated(false);
          clearAuthToken();
          throw new Error("Unauthorized - please login");
        }

        if (!response.ok) {
          throw new Error(
            `Network response was not ok: ${response.status} ${response.statusText}`
          );
        }
        const ct = response.headers.get("content-type") || "";
        if (!ct.includes("application/json")) {
          // Common failure mode: HTML (index.html) or a proxy error page.
          setNeedsPairing(true);
          return null;
        }
        const data = await response.json();
        const configWithAPI = { ...data, apiURL };

        // Only update state if config has actually changed
        setConfig((prevConfig) =>
          configsAreEqual(prevConfig, configWithAPI)
            ? prevConfig
            : configWithAPI
        );

        // Only update localConfig if there are no unsaved changes or if forced
        setLocalConfig((prevLocalConfig) => {
          // Always initialize if localConfig is null (first load)
          if (prevLocalConfig === null) {
            return configWithAPI;
          }

          // If forcing update, check if config changed
          if (forceUpdate) {
            return configsAreEqual(prevLocalConfig, configWithAPI)
              ? prevLocalConfig
              : configWithAPI;
          }

          // If not forcing, keep existing local config (preserve unsaved changes)
          return prevLocalConfig;
        });

        setDeviceOnline(true); // Device is online
        setAuthenticated(true); // Successfully authenticated
        return data;
      } catch (error) {
        // Handle authentication errors
        if (error.message.includes("Unauthorized")) {
          setAuthenticated(false);
          return null;
        }

        // Only log errors if we're not already offline to avoid spam
        if (deviceOnlineRef.current) {
          console.warn(
            getUserFriendlyErrorMessage(error, "fetching configuration")
          );
        }
        setDeviceOnline(false);
        return null;
      } finally {
        isFetchingConfigRef.current = false;
      }
    },
    [apiURL, configsAreEqual]
  );

  // Keep a stable reference so effects can depend only on networkEnabled and
  // not re-run due to function identity changes.
  const fetchConfigRef = useRef(fetchConfig);
  useEffect(() => {
    fetchConfigRef.current = fetchConfig;
  }, [fetchConfig]);

  /**
   * Check authentication status on application mount
   * Includes retry logic for device restarts
   */
  useEffect(() => {
    if (!networkEnabled) return;
    // On espwifi.io we operate in pairing/cloud mode; no direct device HTTP auth.
    if (isCloudDashboard) {
      setAuthChecked(true);
      setCheckingAuth(false);
      return;
    }
    // LAN/dev and device-hosted: if we have a control WS target, config/info comes
    // from /ws/control and we don't need HTTP auth/config bootstrapping.
    if (controlWsUrl) {
      setNeedsPairing(false);
      setAuthenticated(true);
      setAuthChecked(true);
      setCheckingAuth(false);
      return;
    }
    const checkAuth = async () => {
      setCheckingAuth(true);

      try {
        // Retry logic to handle device restarts
        const hasToken = isAuthenticated();
        // If we don't have a token, extra retries just delay showing Login.
        // If we DO have a token, retries can help survive device restarts.
        const maxRetries = hasToken ? 5 : 1;
        const retryDelay = 1000; // 1 second between retries
        let result = null;

        for (let attempt = 0; attempt < maxRetries; attempt++) {
          result = await fetchConfigRef.current?.();
          if (result) break;

          // If we didn't succeed and there are retries left, wait before retrying
          if (attempt < maxRetries - 1) {
            await new Promise((resolve) => setTimeout(resolve, retryDelay));
          }
        }

        // If all retries failed, set authenticated to false
        if (!result) {
          setAuthenticated(false);
        }
      } catch (error) {
        console.error("Error during auth check:", error);
        setAuthenticated(false);
      } finally {
        setCheckingAuth(false);
        setAuthChecked(true);
      }
    };

    checkAuth();
  }, [networkEnabled, isCloudDashboard, controlWsUrl]);

  /**
   * Set up polling for config updates when authenticated
   * Uses exponential backoff for failed requests to avoid overwhelming the device
   */
  useEffect(() => {
    // Only start polling if authenticated
    if (!networkEnabled || !authenticated || isCloudDashboard || controlWsUrl) {
      return;
    }

    let pollTimeout;
    let consecutiveFailures = 0;
    const maxConsecutiveFailures = 5; // Stop polling after 5 consecutive failures
    const basePollInterval = 30000; // Base: 30 seconds (much more conservative)
    const maxPollInterval = 120000; // Max: 2 minutes
    let isActive = true; // Flag to check if effect is still active

    const scheduleNextPoll = (intervalMs) => {
      if (!isActive) return;

      pollTimeout = setTimeout(async () => {
        // Skip if a fetch is already in progress
        if (isFetchingConfigRef.current) {
          console.log("Skipping poll - fetch already in progress");
          // Try again after a short delay
          scheduleNextPoll(5000);
          return;
        }

        try {
          const result = await fetchConfigRef.current?.();

          if (result) {
            consecutiveFailures = 0; // Reset failure count on success
            scheduleNextPoll(basePollInterval); // Use base interval
          } else {
            consecutiveFailures++;

            // Stop polling after max consecutive failures
            if (consecutiveFailures >= maxConsecutiveFailures) {
              console.warn(
                `Stopping polling after ${maxConsecutiveFailures} consecutive failures. Device appears offline.`
              );
              return; // Stop polling
            }

            // Exponential backoff: double the interval for each failure, up to max
            const backoffInterval = Math.min(
              basePollInterval * Math.pow(2, consecutiveFailures - 1),
              maxPollInterval
            );
            console.log(
              `Poll failed (${consecutiveFailures}/${maxConsecutiveFailures}). Next poll in ${
                backoffInterval / 1000
              }s`
            );
            scheduleNextPoll(backoffInterval);
          }
        } catch (error) {
          console.error("Error in polling loop:", error);
          consecutiveFailures++;

          if (consecutiveFailures < maxConsecutiveFailures) {
            const backoffInterval = Math.min(
              basePollInterval * Math.pow(2, consecutiveFailures - 1),
              maxPollInterval
            );
            scheduleNextPoll(backoffInterval);
          }
        }
      }, intervalMs);
    };

    // Start polling
    scheduleNextPoll(basePollInterval);

    // Cleanup on unmount
    return () => {
      isActive = false;
      if (pollTimeout) {
        clearTimeout(pollTimeout);
      }
    };
  }, [authenticated, networkEnabled, isCloudDashboard, controlWsUrl]);

  /**
   * Handle successful login
   * Fetches initial config and removes loading screen
   */
  const handleLoginSuccess = () => {
    setAuthenticated(true);
    fetchConfigRef.current?.(true);
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
      // One mode: send config over control WS (no HTTP /api/config).
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
            needsPairing || deviceOnline ? "secondary.light" : "error.main",
          color: needsPairing || deviceOnline ? "primary.main" : "white",
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
        }}
      >
        <Box sx={{ display: "flex", alignItems: "center", gap: 2 }}>
          {localConfig?.["deviceName"] || config?.["deviceName"] || "ESPWiFi"}
          {selectedDeviceId ? (
            <Typography
              variant="caption"
              sx={{ opacity: 0.75, fontFamily: "inherit" }}
            >
              Control: {controlConnected ? "connected" : "disconnected"}
            </Typography>
          ) : null}
        </Box>
      </Container>

      {deviceApiBlocked && (
        <Container sx={{ mt: 2 }}>
          <Box
            sx={{
              border: "1px solid",
              borderColor: "warning.main",
              bgcolor: "rgba(255, 167, 38, 0.12)",
              p: 2,
              borderRadius: 2,
              maxWidth: 920,
              mx: "auto",
            }}
          >
            <Typography variant="h6" sx={{ fontWeight: 700, mb: 0.5 }}>
              Device API blocked on HTTPS
            </Typography>
            <Typography variant="body2" sx={{ opacity: 0.9 }}>
              This page is loaded over HTTPS, but the device API target is HTTP
              (mixed content). Mobile browsers usually block this.
            </Typography>
            <Typography variant="body2" sx={{ mt: 1, opacity: 0.9 }}>
              Try opening the device UI directly on your LAN (e.g.{" "}
              <code>http://espwifi.local</code> or the device IP), or enable the
              cloud tunnel for WebSocket features.
            </Typography>
            {deviceApiBlockedUrl ? (
              <Typography
                variant="caption"
                sx={{ display: "block", mt: 1, opacity: 0.75 }}
              >
                Blocked URL: {deviceApiBlockedUrl}
              </Typography>
            ) : null}
          </Box>
        </Container>
      )}

      {needsPairing && !deviceApiBlocked && (
        <Container sx={{ mt: 2 }}>
          <Box
            sx={{
              border: "1px solid",
              borderColor: "info.main",
              bgcolor: "rgba(2, 136, 209, 0.12)",
              p: 2,
              borderRadius: 2,
              maxWidth: 920,
              mx: "auto",
            }}
          >
            <Typography variant="h6" sx={{ fontWeight: 700, mb: 0.5 }}>
              {devices.length > 0 ? "Select a device" : "No device paired"}
            </Typography>
            <Typography variant="body2" sx={{ opacity: 0.9 }}>
              {isCloudDashboard ? (
                <>
                  You’re on <code>espwifi.io</code>, which can’t directly reach
                  a LAN device at <code>espwifi.local</code> from a
                  phone/desktop browser. Tap <b>Devices</b> below to select a
                  saved device or pair a new one.
                </>
              ) : (
                <>
                  You’re running the dashboard at{" "}
                  <code>{window.location.host}</code>. Tap <b>Devices</b> below
                  to select a saved device (paired tunnel mode) or pair a new
                  one.
                </>
              )}
            </Typography>
          </Box>
        </Container>
      )}

      {/* Settings Button Bar */}
      <Suspense fallback={null}>
        <SettingsButtonBar
          config={effectiveConfig}
          deviceOnline={deviceOnline}
          saveConfig={updateLocalConfig}
          saveConfigToDevice={saveConfigFromButton}
          getRSSIColor={getRSSIColor}
          getRSSIIcon={getRSSIIcon}
          controlRssi={cloudRssi}
          logsText={cloudLogs}
          logsError={cloudLogsError}
          onRequestRssi={() => {
            const ws = controlWsRef.current;
            if (!ws || ws.readyState !== 1) return;
            try {
              ws.send(JSON.stringify({ cmd: "get_rssi" }));
            } catch {
              // ignore
            }
          }}
          onRequestLogs={() => {
            const ws = controlWsRef.current;
            if (!ws || ws.readyState !== 1) return;
            try {
              ws.send(
                JSON.stringify({
                  cmd: "get_logs",
                  tailBytes: 256 * 1024,
                  // Keep small by default; tunnel JSON escaping can expand.
                  maxBytes: 2 * 1024,
                })
              );
            } catch {
              // ignore
            }
          }}
          cameraEnabled={effectiveConfig?.camera?.enabled || false}
          getCameraColor={() =>
            effectiveConfig?.camera?.enabled ? "primary.main" : "text.disabled"
          }
          devices={devices}
          selectedDeviceId={selectedDeviceId}
          onSelectDevice={handleSelectDevice}
          onRemoveDevice={handleRemoveDevice}
          onPairNewDevice={handlePairNewDevice}
          cloudMode={Boolean(controlUiUrl)}
          controlConnected={controlConnected}
          deviceInfoOverride={cloudDeviceInfo}
        />
      </Suspense>

      {pairingOpen && (
        <Suspense fallback={null}>
          <BlePairingDialog
            open={pairingOpen}
            onClose={() => setPairingOpen(false)}
            onPaired={(record) => {
              setDevices((prev) => {
                const next = upsertDevice(prev, record);
                saveDevices(next);
                return next;
              });
              handleSelectDevice(record);
            }}
          />
        </Suspense>
      )}

      <Container>
        <Suspense fallback={<LinearProgress color="inherit" />}>
          <Modules
            config={effectiveConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={deviceOnline}
          />
        </Suspense>
      </Container>

      {/* Show login modal when not authenticated */}
      {!authenticated &&
        !deviceApiBlocked &&
        !needsPairing &&
        authChecked &&
        !checkingAuth && (
          <Suspense fallback={null}>
            <Login onLoginSuccess={handleLoginSuccess} />
          </Suspense>
        )}
    </ThemeProvider>
  );
}

export default App;
