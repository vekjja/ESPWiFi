import React, { useState, useEffect, useCallback } from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import Box from "@mui/material/Box";
import LinearProgress from "@mui/material/LinearProgress";
import { Save, Delete, Edit, Settings } from "@mui/icons-material";
import Modules from "./components/Modules";
import SettingsButtonBar from "./components/SettingsButtonBar";

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
      main: "#FC2828",
    },
    success: {
      main: "#17EB9D",
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

  const hostname = process.env.REACT_APP_API_HOST || "localhost";
  const port = process.env.REACT_APP_API_PORT || 80;
  const apiURL =
    process.env.NODE_ENV === "production" ? "" : `http://${hostname}:${port}`;

  // Function to fetch config from device
  const fetchConfig = useCallback(
    async (forceUpdate = false) => {
      try {
        // Create an AbortController for timeout
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), 6000); // 6 second timeout

        const response = await fetch(apiURL + "/config", {
          signal: controller.signal,
        });

        clearTimeout(timeoutId);

        if (!response.ok) {
          throw new Error(
            `Network response was not ok: ${response.status} ${response.statusText}`
          );
        }
        const data = await response.json();
        const configWithAPI = { ...data, apiURL };

        // Only update state if config has actually changed
        setConfig((prevConfig) => {
          if (JSON.stringify(prevConfig) !== JSON.stringify(configWithAPI)) {
            return configWithAPI;
          }
          return prevConfig;
        });

        // Only update localConfig if there are no unsaved changes or if forced
        setLocalConfig((prevLocalConfig) => {
          // Check if there are unsaved changes by comparing with the main config
          const hasUnsavedChanges =
            JSON.stringify(prevLocalConfig) !== JSON.stringify(config);

          if (forceUpdate || !hasUnsavedChanges) {
            if (
              JSON.stringify(prevLocalConfig) !== JSON.stringify(configWithAPI)
            ) {
              return configWithAPI;
            }
          }
          return prevLocalConfig;
        });

        setDeviceOnline(true); // Device is online
        return data;
      } catch (error) {
        // Only log errors if we're not already offline to avoid spam
        if (deviceOnline) {
          if (error.name === "AbortError") {
            console.warn("Device offline: Request timeout (6 seconds)");
          } else if (
            error.name === "TypeError" &&
            error.message.includes("Failed to fetch")
          ) {
            console.warn("Device offline: Network connection failed");
          } else {
            console.warn("Device offline:", error.message);
          }
        }
        setDeviceOnline(false); // Device is offline
        return null;
      }
    },
    [apiURL, config, deviceOnline]
  );

  useEffect(() => {
    // Initial fetch
    fetchConfig().then(() => {
      setLoading(false);
    });

    // Set up intelligent polling
    let pollInterval;
    let retryCount = 0;
    const maxRetries = 3;

    const startPolling = () => {
      pollInterval = setInterval(
        () => {
          fetchConfig().then((result) => {
            if (result) {
              retryCount = 0; // Reset retry count on successful fetch
            } else if (deviceOnline) {
              retryCount++;
              if (retryCount >= maxRetries) {
                console.warn(
                  "Device appears to be offline - reducing polling frequency"
                );
                clearInterval(pollInterval);
                // Poll less frequently when offline
                pollInterval = setInterval(() => {
                  fetchConfig();
                }, 15000); // 15 seconds when offline
              }
            }
          });
        },
        deviceOnline ? 5000 : 15000
      ); // 5 seconds when online, 15 seconds when offline
    };

    startPolling();

    // Cleanup interval on unmount
    return () => {
      if (pollInterval) {
        clearInterval(pollInterval);
      }
    };
  }, [fetchConfig, deviceOnline]);

  if (loading) {
    return <LinearProgress color="inherit" />;
  }

  // Update local config only (no ESP32 calls)
  const updateLocalConfig = (newConfig) => {
    const configWithAPI = { ...newConfig, apiURL };
    setLocalConfig(configWithAPI);
    // console.log("Local config updated:", configWithAPI);
  };

  // Save config from ConfigButton (updates local config and saves to device)
  const saveConfigFromButton = (newConfig) => {
    // First update local config
    const configWithAPI = { ...newConfig, apiURL };
    setLocalConfig(configWithAPI);

    // Don't attempt to save if device is offline
    if (!deviceOnline) {
      console.warn("Device is offline - configuration saved locally only");
      return;
    }

    // Then save to device
    setSaving(true);
    const configToSave = { ...configWithAPI };
    delete configToSave.apiURL; // Remove the apiURL key entirely

    // Create an AbortController for timeout
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 10000); // 10 second timeout for saves

    fetch(apiURL + "/config", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(configToSave),
      signal: controller.signal,
    })
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
        const configWithAPI = { ...savedConfig, apiURL };
        setConfig(configWithAPI);
        setLocalConfig(configWithAPI);
        console.log("Configuration Saved to Device:", savedConfig);
        setSaving(false);
        // Force a fresh fetch to ensure we have the latest config
        fetchConfig(true);
      })
      .catch((error) => {
        clearTimeout(timeoutId);
        if (error.name === "AbortError") {
          console.error("Save operation timed out - device may be offline");
          alert(
            "Save operation timed out. Device may be offline. Configuration saved locally."
          );
        } else {
          console.error("Error saving configuration to Device:", error);
          alert(`Failed to save configuration to Device: ${error.message}`);
        }
        setSaving(false);
      });
  };

  // Settings button handlers
  const handleNetworkSettings = () => {}; // Device settings now handled by DeviceSettingsButton
  const handleCameraSettings = () => {
    // Toggle camera state immediately
    const newEnabledState = !localConfig?.camera?.enabled;
    const configToSave = {
      ...localConfig,
      camera: {
        ...localConfig.camera,
        enabled: newEnabledState,
        frameRate: localConfig.camera?.frameRate || 10,
      },
    };
    saveConfigFromButton(configToSave);
  };
  const handleRSSISettings = () => {}; // RSSI settings now handled by RSSIButton
  const handleFileBrowser = () => {}; // File browser is now handled by FileBrowserButton
  const handleAddModule = () => {}; // Add module now handled by AddModuleButton

  // RSSI helper functions
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

  const getRSSIIcon = (rssi) => {
    if (rssi === null || rssi === undefined) {
      return null; // Will use default SignalCellularAlt icon
    }
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
          {localConfig?.["mdns"] || config?.["mdns"]}
        </Box>
      </Container>

      {localConfig && (
        <>
          {/* Responsive Settings Button Bar */}
          <SettingsButtonBar
            config={localConfig}
            deviceOnline={deviceOnline}
            onNetworkSettings={handleNetworkSettings}
            onCameraSettings={handleCameraSettings}
            onRSSISettings={handleRSSISettings}
            onFileBrowser={handleFileBrowser}
            onAddModule={handleAddModule}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            onRSSIDataChange={() => {}} // RSSI data is now handled internally by RSSIButton
            rssiDisplayMode={localConfig?.rssi?.displayMode || "numbers"}
            getRSSIColor={getRSSIColor}
            getRSSIIcon={getRSSIIcon}
            // Camera specific props
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
        </>
      )}
    </ThemeProvider>
  );
}

export default App;
