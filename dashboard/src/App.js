import React, { useState, useEffect, useCallback } from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import Box from "@mui/material/Box";
import LinearProgress from "@mui/material/LinearProgress";
import NetworkSettingsModal from "./components/NetworkSettingsModal";
import CameraSettingsModal from "./components/CameraSettingsModal";
import RSSISettingsModal from "./components/RSSISettingsModal";
import MicrophoneSettings from "./components/MicrophoneSettings";
import AddModule from "./components/AddModule";
import Modules from "./components/Modules";
import FileBrowserButton from "./components/FileBrowserButton";

// Define the theme
const theme = createTheme({
  typography: {
    fontFamily: [
      "-apple-system",
      "BlinkMacSystemFont",
      '"Segoe UI"',
      "Roboto Slab",
      '"Helvetica Neue"',
      "Arial",
      "sans-serif",
      '"Apple Color Emoji"',
      '"Segoe UI Emoji"',
      '"Segoe UI Symbol"',
    ].join(","),
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
          fontFamily: "Roboto Slab",
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
          position: "relative",
        }}
      >
        <Box sx={{ display: "flex", alignItems: "center", gap: 2 }}>
          {localConfig?.["mdns"] || config?.["mdns"]}
        </Box>
      </Container>

      {localConfig && (
        <Container>
          <NetworkSettingsModal
            config={localConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={deviceOnline}
          />
          <CameraSettingsModal
            config={localConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={deviceOnline}
          />
          <RSSISettingsModal
            config={localConfig}
            saveConfig={updateLocalConfig}
            saveConfigToDevice={saveConfigFromButton}
            deviceOnline={deviceOnline}
          />
          <MicrophoneSettings
            config={localConfig}
            saveConfig={updateLocalConfig}
            deviceOnline={deviceOnline}
          />
          <AddModule
            config={localConfig}
            saveConfig={updateLocalConfig}
            deviceOnline={deviceOnline}
          />
          <Modules
            config={localConfig}
            saveConfig={updateLocalConfig}
            deviceOnline={deviceOnline}
          />
          <FileBrowserButton config={localConfig} deviceOnline={deviceOnline} />
        </Container>
      )}
    </ThemeProvider>
  );
}

export default App;
