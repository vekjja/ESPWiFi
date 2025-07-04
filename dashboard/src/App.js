import React, { useState, useEffect } from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import LinearProgress from "@mui/material/LinearProgress";
import NetworkSettings from "./components/NetworkSettings";
import AddModule from "./components/AddModule";
import ConfigButton from "./components/ConfigButton";
import Modules from "./components/Modules";

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

  const hostname = process.env.REACT_APP_API_HOST || "localhost";
  const port = process.env.REACT_APP_API_PORT || 80;
  const apiURL =
    process.env.NODE_ENV === "production" ? "" : `http://${hostname}:${port}`;

  useEffect(() => {
    console.log("Fetching configuration from:", apiURL + "/config");
    fetch(apiURL + "/config")
      .then((response) => {
        if (!response.ok) {
          throw new Error("Network response was not ok");
        }
        return response.json();
      })
      .then((data) => {
        console.log("Fetched Configuration:", data);
        const configWithAPI = { ...data, apiURL };
        setConfig(configWithAPI);
        setLocalConfig(configWithAPI);
        setLoading(false);
      })
      .catch((error) => {
        console.error("Error loading configuration:", error);
        setLoading(false);
      });
  }, []);

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
          backgroundColor: "secondary.light",
          color: "primary.main",
          fontSize: "3em",
          height: "9vh",
          zIndex: 1000,
          textAlign: "center",
          display: "flex",
          alignItems: "center",
          justifyContent: "center",
          minWidth: "100%",
        }}
      >
        {localConfig?.["mdns"] || config?.["mdns"]}
      </Container>

      {localConfig && (
        <Container>
          <NetworkSettings
            config={localConfig}
            saveConfig={updateLocalConfig}
          />
          <ConfigButton
            config={localConfig}
            saveConfig={saveConfigFromButton}
          />
          <AddModule config={localConfig} saveConfig={updateLocalConfig} />
          <Modules config={localConfig} saveConfig={updateLocalConfig} />
        </Container>
      )}
    </ThemeProvider>
  );
}

export default App;
