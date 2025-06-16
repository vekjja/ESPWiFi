import React, { useState, useEffect } from "react";
import { createTheme, ThemeProvider } from "@mui/material/styles";
import Container from "@mui/material/Container";
import LinearProgress from "@mui/material/LinearProgress";
import Settings from "./Settings";
import Pins from "./pins/Pins";

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
      main: "#ff3838",
    },
  },
});

function App() {
  const [config, setConfig] = useState(null);
  const [loading, setLoading] = useState(true);

  const hostname = process.env.REACT_APP_API_HOST || "localhost";
  const port = process.env.REACT_APP_API_PORT || 80;
  const apiURL =
    process.env.NODE_ENV === "production" ? "" : `http://${hostname}:${port}`;

  useEffect(() => {
    fetch(apiURL + "/config")
      .then((response) => {
        if (!response.ok) {
          throw new Error("Network response was not ok");
        }
        return response.json();
      })
      .then((data) => {
        data.apiURL = apiURL;
        setConfig(data);
        setLoading(false);
      })
      .catch((error) => {
        console.error("Error loading configuration:", error);
        setLoading(false);
      });
  }, []);

  if (loading) {
    return <LinearProgress color="inherit" />;
    // return <div> Loading... </div>
  }

  const saveConfig = (newConfig) => {
    fetch(apiURL + "/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(newConfig),
    })
      .then((response) => {
        if (!response.ok) throw new Error("Failed to save configuration");
        return response.json();
      })
      .then((savedConfig) => {
        setConfig(savedConfig);
        console.log("Configuration saved successfully:", savedConfig);
      })
      .catch((error) => {
        console.error("Error saving configuration:", error);
        alert("Failed to save configuration");
      });
  };

  return (
    <ThemeProvider theme={theme}>
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
        {config["mdns"]}
      </Container>
      <Container>
        <Settings config={config} saveConfig={saveConfig} />
        <Pins config={config} saveConfig={saveConfig} />
      </Container>
    </ThemeProvider>
  );
}

export default App;
