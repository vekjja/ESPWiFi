import React, { useState, useEffect, useCallback, useRef } from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import Box from "@mui/material/Box";
import LinearProgress from "@mui/material/LinearProgress";
import { Save, Delete, Edit, Settings } from "@mui/icons-material";
import Modules from "./components/Modules";
import Login from "./components/Login";
import SettingsButtonBar from "./components/SettingsButtonBar";
import { getApiUrl, buildApiUrl, getFetchOptions } from "./utils/apiUtils";
import { isAuthenticated, clearAuthToken } from "./utils/authUtils";
import { getRSSIThemeColor } from "./utils/rssiUtils";
import { getUserFriendlyErrorMessage, logError } from "./utils/errorUtils";

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
  // Custom theme properties for icons and components
  custom: {
    icons: {
      save: Save,
      delete: Delete,
      edit: Edit,
      settings: Settings,
    },
  },
});

function App() {
  const [config, setConfig] = useState(null);
  const [localConfig, setLocalConfig] = useState(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [deviceOnline, setDeviceOnline] = useState(true);
  const [authenticated, setAuthenticated] = useState(false);
  const [checkingAuth, setCheckingAuth] = useState(true);

  const apiURL = getApiUrl();

  // Use ref for fetch locking to avoid race conditions with async state updates
  const isFetchingConfigRef = useRef(false);

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
        // Create an AbortController for timeout
        const controller = new AbortController();
        // Timeout set to 20 seconds to handle slow device responses
        const timeoutId = setTimeout(() => controller.abort(), 20000);

        const response = await fetch(
          buildApiUrl("/api/config"),
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
        if (deviceOnline) {
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
    [apiURL, deviceOnline, configsAreEqual]
  );

  /**
   * Check authentication status on application mount
   * Includes retry logic for device restarts
   */
  useEffect(() => {
    const checkAuth = async () => {
      setCheckingAuth(true);
      setLoading(true);

      try {
        // Small delay to ensure loading bar is visible
        await new Promise((resolve) => setTimeout(resolve, 100));

        // Retry logic to handle device restarts
        const maxRetries = 5;
        const retryDelay = 1000; // 1 second between retries
        let result = null;

        for (let attempt = 0; attempt < maxRetries; attempt++) {
          // If we have a token, try to fetch config to verify it's valid
          if (isAuthenticated()) {
            result = await fetchConfig();
            if (result) {
              setAuthenticated(true);
              break;
            }
          } else {
            // No token, try to fetch config anyway (auth might be disabled)
            result = await fetchConfig();
            if (result) {
              setAuthenticated(true);
              break;
            }
          }

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
        // Always set these to false to exit loading state
        setCheckingAuth(false);
        setLoading(false);
      }
    };

    checkAuth();
  }, [fetchConfig]);

  /**
   * Set up polling for config updates when authenticated
   * Uses exponential backoff for failed requests to avoid overwhelming the device
   */
  useEffect(() => {
    // Only start polling if authenticated
    if (!authenticated) {
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
          const result = await fetchConfig();

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
  }, [fetchConfig, authenticated]);

  /**
   * Handle successful login
   * Fetches initial config and removes loading screen
   */
  const handleLoginSuccess = () => {
    setAuthenticated(true);
    fetchConfig(true).then(() => {
      setLoading(false);
    });
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
        return;
      }

      // Then save to device
      setSaving(true);
      const configToSave = { ...newConfig };

      // Create an AbortController for timeout
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 10000);

      fetch(
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
        .then((savedConfig) => {
          setSaving(false);
          // No need to update state - the config was already updated locally before the save
          // and the periodic polling will sync any server-side changes
        })
        .catch((error) => {
          clearTimeout(timeoutId);
          const errorMessage = getUserFriendlyErrorMessage(
            error,
            "saving configuration"
          );
          logError(error, "Config Save", true);
          alert(`${errorMessage} Configuration saved locally.`);
          setSaving(false);
        });
    },
    [apiURL, deviceOnline]
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

  // Show loading indicator while checking auth or loading initial config
  if (loading || checkingAuth) {
    return <LinearProgress color="inherit" />;
  }

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

      <Container
        sx={{
          fontFamily: theme.typography.headerFontFamily,
          backgroundColor: deviceOnline ? "secondary.light" : "error.main",
          color: deviceOnline ? "primary.main" : "white",
          fontSize: "3em",
          height: "9vh",
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

      {/* Settings Button Bar */}
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

      <Container>
        <Modules
          config={localConfig}
          saveConfig={updateLocalConfig}
          saveConfigToDevice={saveConfigFromButton}
          deviceOnline={deviceOnline}
        />
      </Container>

      {/* Show login modal when not authenticated */}
      {!authenticated && !checkingAuth && (
        <Login onLoginSuccess={handleLoginSuccess} />
      )}
    </ThemeProvider>
  );
}

export default App;
