import React, {
  useState,
  useEffect,
  useCallback,
  useRef,
  Suspense,
  lazy,
} from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import Box from "@mui/material/Box";
import LinearProgress from "@mui/material/LinearProgress";
import Typography from "@mui/material/Typography";
import { getApiUrl, buildApiUrl, getFetchOptions } from "./utils/apiUtils";
import { isAuthenticated, clearAuthToken } from "./utils/authUtils";
import { getRSSIThemeColor } from "./utils/rssiUtils";
import { getUserFriendlyErrorMessage, logError } from "./utils/errorUtils";

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
  const [saving, setSaving] = useState(false);
  const [deviceOnline, setDeviceOnline] = useState(true);
  const [authenticated, setAuthenticated] = useState(false);
  const [checkingAuth, setCheckingAuth] = useState(false);
  const [networkEnabled, setNetworkEnabled] = useState(false);
  const [authChecked, setAuthChecked] = useState(false);
  const [deviceApiBlocked, setDeviceApiBlocked] = useState(false);
  const [deviceApiBlockedUrl, setDeviceApiBlockedUrl] = useState("");

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
        const configUrl = buildApiUrl("/api/config");
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
  }, [networkEnabled]);

  /**
   * Set up polling for config updates when authenticated
   * Uses exponential backoff for failed requests to avoid overwhelming the device
   */
  useEffect(() => {
    // Only start polling if authenticated
    if (!networkEnabled || !authenticated) {
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
  }, [authenticated, networkEnabled]);

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
  };

  /**
   * Save configuration to device and update local state
   * Used by buttons and components that need to persist changes
   * Includes timeout handling and offline detection
   * @param {Object} newConfig - The new configuration to save
   */
  const saveConfigFromButton = useCallback(
    (newConfig) => {
      // First update local config by merging with existing config
      // This handles both full and partial config updates
      setLocalConfig((prevConfig) => ({ ...prevConfig, ...newConfig, apiURL }));

      // Don't attempt to save if device is offline
      if (!deviceOnline) {
        console.warn("Device is offline - configuration saved locally only");
        return Promise.resolve(null);
      }

      // Then save to device
      setSaving(true);
      const configToSave = { ...newConfig };

      // Create an AbortController for timeout
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 10000);

      return fetch(
        buildApiUrl("/api/config"),
        getFetchOptions({
          method: "PUT",
          body: JSON.stringify(configToSave),
          signal: controller.signal,
        })
      )
        .then((response) => {
          clearTimeout(timeoutId);
          if (!response.ok) {
            throw new Error(
              `Failed to save configuration to Device: ${response.status} ${response.statusText}`
            );
          }
          return response.json();
        })
        .then(async () => {
          // Immediately refetch config so device-generated fields (e.g. auth.token)
          // show up in the UI without waiting for polling.
          await fetchConfig(true);
          return true;
        })
        .catch((error) => {
          clearTimeout(timeoutId);
          const errorMessage = getUserFriendlyErrorMessage(
            error,
            "saving configuration"
          );
          logError(error, "Config Save", true);
          alert(`${errorMessage} Configuration saved locally.`);
          return false;
        })
        .finally(() => {
          setSaving(false);
        });
    },
    [apiURL, deviceOnline, fetchConfig]
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
          backgroundColor: deviceOnline ? "secondary.light" : "error.main",
          color: deviceOnline ? "primary.main" : "white",
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

      {/* Settings Button Bar */}
      <Suspense fallback={null}>
        <SettingsButtonBar
          config={localConfig}
          deviceOnline={deviceOnline}
          saveConfig={updateLocalConfig}
          saveConfigToDevice={saveConfigFromButton}
          getRSSIColor={getRSSIColor}
          getRSSIIcon={getRSSIIcon}
          cameraEnabled={localConfig?.camera?.enabled || false}
          getCameraColor={() =>
            localConfig?.camera?.enabled ? "primary.main" : "text.disabled"
          }
        />
      </Suspense>

      <Container>
        <Suspense fallback={<LinearProgress color="inherit" />}>
          <Modules
            config={localConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={deviceOnline}
          />
        </Suspense>
      </Container>

      {/* Show login modal when not authenticated */}
      {!authenticated && !deviceApiBlocked && authChecked && !checkingAuth && (
        <Suspense fallback={null}>
          <Login onLoginSuccess={handleLoginSuccess} />
        </Suspense>
      )}
    </ThemeProvider>
  );
}

export default App;
