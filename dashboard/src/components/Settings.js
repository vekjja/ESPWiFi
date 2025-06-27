import React, { useState, useEffect } from "react";
import {
  Container,
  Fab,
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  Tooltip,
  TextField,
  FormControl,
  FormControlLabel,
  Switch,
} from "@mui/material";
import SettingsIcon from "@mui/icons-material/Settings";
import RestartIcon from "@mui/icons-material/RestartAlt";
import IconButton from "@mui/material/IconButton";
import SaveButton from "./SaveButton";

export default function Pins({ config, saveConfig }) {
  const [isModalOpen, setIsModalOpen] = useState(false); // Modal visibility state
  const [ssid, setSsid] = useState(""); // State for WiFi SSID
  const [mdns, setMdns] = useState(config["mdns"]); // State for mDNS hostname
  const [password, setPassword] = useState(""); // State for WiFi password
  const [apSsid, setApSsid] = useState(""); // State for Access Point SSID
  const [apPassword, setApPassword] = useState(""); // State for Access Point password
  const [mode, setMode] = useState("client"); // State for mode toggle

  useEffect(() => {
    if (config) {
      setSsid(config.client?.ssid || ""); // Safely set SSID
      setPassword(config.client?.password || ""); // Safely set Password
      setApSsid(config.ap?.ssid || ""); // Safely set AP SSID
      setApPassword(config.ap?.password || ""); // Safely set AP Password
      setMode(config.mode || "client"); // Safely set mode
    }
  }, [config]);

  const handleOpenModal = () => {
    setIsModalOpen(true);
  };

  const handleCloseModal = () => {
    setIsModalOpen(false);
  };

  const handleSsidChange = (event) => {
    setSsid(event.target.value);
  };

  const handlePasswordChange = (event) => {
    setPassword(event.target.value);
  };

  const handleApSsidChange = (event) => {
    setApSsid(event.target.value);
  };

  const handleApPasswordChange = (event) => {
    setApPassword(event.target.value);
  };

  const handleModeToggle = (event) => {
    setMode(event.target.checked ? "client" : "ap");
  };

  const handleMDNSChange = (event) => {
    setMdns(event.target.value);
  };

  const isValidHostname = (hostname) => {
    const regex = /^(?!-)[A-Za-z0-9-]{1,63}(?<!-)$/;
    return regex.test(hostname);
  };

  const handleSave = () => {
    if (!isValidHostname(mdns)) {
      alert("Invalid mDNS hostname. Please enter a valid hostname.");
      return;
    }
    // Handle the submit action
    saveConfig({
      ...config,
      mode: mode,
      mdns: mdns,
      client: {
        ssid: ssid,
        password: password,
      },
      ap: {
        ssid: apSsid,
        password: apPassword,
      },
    });
    handleCloseModal();
  };

  const handleRestart = () => {
    fetch(`${config.apiURL}/restart`, {
      method: "GET",
    })
      .then((response) => {
        if (!response.ok) throw new Error("Failed to reboot");
        console.log("Restarting...");
      })
      .catch((error) => {
        console.error("Error restarting:", error);
        alert("Failed to restart");
      });
  };

  return (
    <Container
      sx={{
        display: "flex",
        flexWrap: "wrap",
        justifyContent: "center",
      }}
    >
      <Fab
        size="small"
        color="primary"
        aria-label="add"
        onClick={handleOpenModal}
        sx={{
          position: "fixed",
          top: "20px",
          left: "20px",
        }}
      >
        <SettingsIcon />
      </Fab>

      <Dialog open={isModalOpen} onClose={handleCloseModal}>
        <DialogTitle>Network Settings</DialogTitle>
        <DialogContent>
          <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
            <TextField
              label="mDNS Hostname"
              value={mdns}
              onChange={handleMDNSChange}
              variant="outlined"
              fullWidth
            />
          </FormControl>
          <FormControl variant="outlined" sx={{ marginTop: 1 }}>
            <FormControlLabel
              control={
                <Switch
                  checked={mode === "client"}
                  onChange={handleModeToggle}
                />
              }
              label={mode === "client" ? "WiFi Client" : "Access Point"}
            />
          </FormControl>
          {mode === "client" ? (
            <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
              <TextField
                label="SSID"
                value={ssid}
                onChange={handleSsidChange}
                variant="outlined"
                fullWidth
              />
              <TextField
                label="Password"
                value={password}
                onChange={handlePasswordChange}
                variant="outlined"
                fullWidth
                sx={{ marginTop: 1 }}
              />
            </FormControl>
          ) : (
            <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
              <TextField
                label="SSID"
                value={apSsid}
                onChange={handleApSsidChange}
                variant="outlined"
                fullWidth
              />
              <TextField
                label="Password"
                value={apPassword}
                onChange={handleApPasswordChange}
                variant="outlined"
                fullWidth
                sx={{ marginTop: 1 }}
              />
            </FormControl>
          )}
        </DialogContent>
        <DialogActions>
          <Tooltip title="Restart Device">
            <IconButton onClick={handleRestart} sx={{ mt: 2, ml: 2 }}>
              <RestartIcon />
            </IconButton>
          </Tooltip>
          <SaveButton onSave={handleSave} tooltip={"Save Network Settings"} />
        </DialogActions>
      </Dialog>
    </Container>
  );
}
