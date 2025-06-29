import React, { useState, useEffect } from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import LinearProgress from "@mui/material/LinearProgress";
import { Fab, Tooltip } from "@mui/material";
import SaveIcon from "@mui/icons-material/Save";
import NetworkSettings from "./components/NetworkSettings";
import AddButton from "./components/AddButton";
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
      main: "#3effc1",
    },
    secondary: {
      main: "#333",
    },
    error: {
      main: "#FF4949",
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

  // Check if there are unsaved changes
  const hasUnsavedChanges =
    config &&
    localConfig &&
    JSON.stringify(config) !== JSON.stringify(localConfig);

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
    console.log("Local config updated:", configWithAPI);
  };

  // Save current local config to ESP32
  const saveToDevice = () => {
    if (!localConfig) return;

    setSaving(true);
    const { apiURL: _apiURL, ...configToSave } = localConfig;

    fetch(apiURL + "/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(configToSave),
    })
      .then((response) => {
        if (!response.ok) throw new Error("Failed to save configuration");
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
        console.error("Error saving configuration to ESP32:", error);
        alert("Failed to save configuration to ESP32");
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

      {/* Global Save Button */}
      <Tooltip
        title={
          hasUnsavedChanges
            ? "Save Module Configuration (unsaved changes)"
            : "Save Module Configuration"
        }
      >
        <Fab
          size="small"
          color={hasUnsavedChanges ? "primary" : "secondary"}
          onClick={saveToDevice}
          sx={{
            position: "fixed",
            top: "20px",
            right: "80px", // Position to the left of the Add button
            zIndex: 1001,
            display: hasUnsavedChanges ? "block" : "none",
          }}
        >
          <SaveIcon />
        </Fab>
      </Tooltip>

      {localConfig && (
        <Container>
          <NetworkSettings
            config={localConfig}
            saveConfig={updateLocalConfig}
          />
          <AddButton config={localConfig} saveConfig={updateLocalConfig} />
          <Modules config={localConfig} saveConfig={updateLocalConfig} />
        </Container>
      )}
    </ThemeProvider>
  );
}

export default App;
