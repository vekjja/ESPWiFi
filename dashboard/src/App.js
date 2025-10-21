import React, { useState, useEffect, useCallback } from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import Box from "@mui/material/Box";
import LinearProgress from "@mui/material/LinearProgress";
import NetworkSettingsModal from "./components/NetworkSettingsModal";
import RSSISettingsModal from "./components/RSSISettingsModal";
import AddModuleModal from "./components/AddModuleModal";
import Modules from "./components/Modules";
import FileBrowserButton from "./components/FileBrowserButton";
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
      main: "#FF5656",
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
});

function App() {
  const [config, setConfig] = useState(null);
  const [localConfig, setLocalConfig] = useState(null);
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [deviceOnline, setDeviceOnline] = useState(true);

  // Settings modal states
  const [networkModalOpen, setNetworkModalOpen] = useState(false);
  const [rssiModalOpen, setRssiModalOpen] = useState(false);
  const [fileBrowserModalOpen, setFileBrowserModalOpen] = useState(false);
  const [addModuleModalOpen, setAddModuleModalOpen] = useState(false);

  // RSSI data state
  const [rssiValue, setRssiValue] = useState(null);

  const hostname = process.env.REACT_APP_API_HOST || "localhost";
  const port = process.env.REACT_APP_API_PORT || 80;
  const apiURL =
    process.env.NODE_ENV === "production" ? "" : `http://${hostname}:${port}`;

  // Function to fetch config from device
  const fetchConfig = useCallback(
    async (forceUpdate = false) => {
      try {
        const response = await fetch(apiURL + "/config");
        if (!response.ok) {
          throw new Error("Network response was not ok");
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
        console.error("Device offline:", error.message);
        setDeviceOnline(false); // Device is offline
        return null;
      }
    },
    [apiURL, config]
  );

  useEffect(() => {
    // Initial fetch
    fetchConfig().then(() => {
      setLoading(false);
    });

    // Set up polling every 5 seconds (frequent but smart)
    const pollInterval = setInterval(() => {
      fetchConfig();
    }, 5000);

    // Cleanup interval on unmount
    return () => {
      clearInterval(pollInterval);
    };
  }, [fetchConfig]);

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

    // Then save to device
    setSaving(true);
    const configToSave = { ...configWithAPI };
    delete configToSave.apiURL; // Remove the apiURL key entirely

    fetch(apiURL + "/config", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(configToSave),
    })
      .then((response) => {
        if (!response.ok)
          throw new Error("Failed to save configuration to Device");
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
        console.error("Error saving configuration to Device:", error);
        alert("Failed to save configuration to Device");
        setSaving(false);
      });
  };

  // Settings button handlers
  const handleNetworkSettings = () => setNetworkModalOpen(true);
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
  const handleRSSISettings = () => setRssiModalOpen(true);
  const handleFileBrowser = () => setFileBrowserModalOpen(true);
  const handleAddModule = () => setAddModuleModalOpen(true);

  // Close handlers
  const closeNetworkModal = () => setNetworkModalOpen(false);
  const closeRSSIModal = () => setRssiModalOpen(false);
  const closeFileBrowserModal = () => setFileBrowserModalOpen(false);
  const closeAddModuleModal = () => setAddModuleModalOpen(false);

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
            // RSSI specific props
            rssiValue={rssiValue}
            rssiEnabled={localConfig?.rssi?.enabled || false}
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
              deviceOnline={deviceOnline}
            />
          </Container>

          {/* Settings Modals - Only render when opened by SettingsButtonBar */}
          {networkModalOpen && (
            <NetworkSettingsModal
              config={localConfig}
              saveConfig={updateLocalConfig}
              saveConfigToDevice={saveConfigFromButton}
              deviceOnline={deviceOnline}
              open={networkModalOpen}
              onClose={closeNetworkModal}
            />
          )}
          <RSSISettingsModal
            config={localConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={deviceOnline}
            open={rssiModalOpen}
            onClose={closeRSSIModal}
            onRSSIDataChange={(value, connected) => {
              setRssiValue(value);
            }}
          />
          {addModuleModalOpen && (
            <AddModuleModal
              config={localConfig}
              saveConfig={updateLocalConfig}
              open={addModuleModalOpen}
              onClose={closeAddModuleModal}
            />
          )}
          {fileBrowserModalOpen && (
            <FileBrowserButton
              config={localConfig}
              deviceOnline={deviceOnline}
              open={fileBrowserModalOpen}
              onClose={closeFileBrowserModal}
            />
          )}
        </>
      )}
    </ThemeProvider>
  );
}

export default App;
