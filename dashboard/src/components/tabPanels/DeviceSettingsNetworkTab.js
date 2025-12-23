import React from "react";
import {
  TextField,
  FormControl,
  Box,
  InputAdornment,
  IconButton,
  Tooltip,
  Typography,
} from "@mui/material";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import WifiIcon from "@mui/icons-material/Wifi";
import RouterIcon from "@mui/icons-material/Router";

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
  deviceName,
  setDeviceName,
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

  const handleSelectClientMode = () => {
    setMode("client");
  };

  const handleSelectApMode = () => {
    setMode("accessPoint");
  };

  const handleDeviceNameChange = (event) => {
    setDeviceName(event.target.value);
  };

  const handleTogglePasswordVisibility = () => {
    setShowPassword((prev) => !prev);
  };

  const handleToggleApPasswordVisibility = () => {
    setShowApPassword((prev) => !prev);
  };

  return (
    <>
      <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
        <TextField
          label="Device Name"
          value={deviceName}
          onChange={handleDeviceNameChange}
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
            border: mode === "client" ? 2 : 2,
            borderColor: mode === "client" ? "primary.main" : "transparent",
            borderRadius: 1,
            padding: 2,
            opacity: mode === "client" ? 1 : 0.5,
            transition: "border-color 0.3s ease, opacity 0.3s ease",
          }}
        >
          <Tooltip title="Click to select WiFi Client Mode">
            <Box
              onClick={handleSelectClientMode}
              sx={{
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                gap: 1,
                marginBottom: 1,
                cursor: "pointer",
                "&:hover": {
                  opacity: 0.8,
                },
              }}
            >
              <WifiIcon
                sx={{
                  color: mode === "client" ? "primary.main" : "text.disabled",
                }}
              />
              <Typography
                variant="h6"
                sx={{
                  margin: 0,
                  color: mode === "client" ? "primary.main" : "text.disabled",
                  pointerEvents: "none",
                }}
              >
                WiFi Client
              </Typography>
            </Box>
          </Tooltip>
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
            border: mode === "accessPoint" ? 2 : 2,
            borderColor:
              mode === "accessPoint" ? "primary.main" : "transparent",
            borderRadius: 1,
            padding: 2,
            opacity: mode === "accessPoint" ? 1 : 0.5,
            transition: "border-color 0.3s ease, opacity 0.3s ease",
          }}
        >
          <Tooltip title="Click to select Access Point Mode">
            <Box
              onClick={handleSelectApMode}
              sx={{
                display: "flex",
                alignItems: "center",
                justifyContent: "center",
                gap: 1,
                marginBottom: 1,
                cursor: "pointer",
                "&:hover": {
                  opacity: 0.8,
                },
              }}
            >
              <RouterIcon
                sx={{
                  color:
                    mode === "accessPoint" ? "primary.main" : "text.disabled",
                }}
              />
              <Typography
                variant="h6"
                sx={{
                  margin: 0,
                  color:
                    mode === "accessPoint" ? "primary.main" : "text.disabled",
                  pointerEvents: "none",
                }}
              >
                WiFi Access Point
              </Typography>
            </Box>
          </Tooltip>
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
