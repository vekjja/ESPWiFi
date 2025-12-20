import React from "react";
import {
  TextField,
  FormControl,
  FormControlLabel,
  Switch,
  Box,
  InputAdornment,
  IconButton,
} from "@mui/material";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";

export default function DeviceSettingsNetworkTab({
  ssid,
  setSsid,
  password,
  setPassword,
  apSsid,
  setApSsid,
  apPassword,
  setApPassword,
  mode,
  setMode,
  mdns,
  setMdns,
  showPassword,
  setShowPassword,
  showApPassword,
  setShowApPassword,
}) {
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

  const handleTogglePasswordVisibility = () => {
    setShowPassword((prev) => !prev);
  };

  const handleToggleApPasswordVisibility = () => {
    setShowApPassword((prev) => !prev);
  };

  return (
    <>
      <FormControl variant="outlined" sx={{ marginTop: 1 }}>
        <FormControlLabel
          control={
            <Switch checked={mode === "client"} onChange={handleModeToggle} />
          }
          label={mode === "client" ? "WiFi Client Mode" : "Access Point Mode"}
        />
      </FormControl>
      <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
        <TextField
          label="mDNS Hostname"
          value={mdns}
          onChange={handleMDNSChange}
          variant="outlined"
          fullWidth
        />
      </FormControl>

      <Box
        sx={{
          display: "flex",
          flexDirection: "column",
          marginTop: 2,
        }}
      >
        {/* WiFi Client Settings */}
        <Box
          sx={{
            order: mode === "client" ? 1 : 2,
            border: mode === "client" ? 2 : 2,
            borderColor: mode === "client" ? "primary.main" : "transparent",
            borderRadius: 1,
            padding: 2,
            opacity: mode === "client" ? 1 : 0.5,
            transition: "border-color 0.3s ease, opacity 0.3s ease",
          }}
        >
          <h4 style={{ margin: "0 0 8px 0", color: "primary.secondary" }}>
            WiFi Client Settings
          </h4>
          <FormControl fullWidth variant="outlined">
            <TextField
              label="Client SSID"
              value={ssid}
              onChange={handleSsidChange}
              variant="outlined"
              fullWidth
            />
            <TextField
              type={showPassword ? "text" : "password"}
              label="Client Password"
              value={password}
              onChange={handlePasswordChange}
              variant="outlined"
              fullWidth
              sx={{ marginTop: 1 }}
              InputProps={{
                endAdornment: (
                  <InputAdornment position="end">
                    <IconButton
                      onClick={handleTogglePasswordVisibility}
                      edge="end"
                    >
                      {showPassword ? (
                        <VisibilityOffIcon />
                      ) : (
                        <VisibilityIcon />
                      )}
                    </IconButton>
                  </InputAdornment>
                ),
              }}
            />
          </FormControl>
        </Box>

        {/* Access Point Settings */}
        <Box
          sx={{
            marginTop: 2,
            order: mode === "ap" ? 1 : 2,
            border: mode === "ap" ? 2 : 2,
            borderColor: mode === "ap" ? "primary.main" : "transparent",
            borderRadius: 1,
            padding: 2,
            opacity: mode === "ap" ? 1 : 0.5,
            transition: "border-color 0.3s ease, opacity 0.3s ease",
          }}
        >
          <h4 style={{ margin: "0 0 8px 0", color: "primary.main" }}>
            WiFi Access Point Settings
          </h4>
          <FormControl fullWidth variant="outlined">
            <TextField
              label="AP SSID"
              value={apSsid}
              onChange={handleApSsidChange}
              variant="outlined"
              fullWidth
            />
            <TextField
              type={showApPassword ? "text" : "password"}
              label="AP Password"
              value={apPassword}
              onChange={handleApPasswordChange}
              variant="outlined"
              fullWidth
              sx={{ marginTop: 1 }}
              InputProps={{
                endAdornment: (
                  <InputAdornment position="end">
                    <IconButton
                      onClick={handleToggleApPasswordVisibility}
                      edge="end"
                    >
                      {showApPassword ? (
                        <VisibilityOffIcon />
                      ) : (
                        <VisibilityIcon />
                      )}
                    </IconButton>
                  </InputAdornment>
                ),
              }}
            />
          </FormControl>
        </Box>
      </Box>
    </>
  );
}
