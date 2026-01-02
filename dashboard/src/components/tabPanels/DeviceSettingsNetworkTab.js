import React from "react";
import {
  TextField,
  FormControl,
  Box,
  InputAdornment,
  IconButton,
  Tooltip,
  Typography,
  Select,
  MenuItem,
  InputLabel,
  Alert,
  Link,
} from "@mui/material";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import WifiIcon from "@mui/icons-material/Wifi";
import RouterIcon from "@mui/icons-material/Router";
import BoltIcon from "@mui/icons-material/Bolt";

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
  txPower,
  setTxPower,
  powerSave,
  setPowerSave,
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

  const handleTxPowerChange = (event) => {
    setTxPower(parseFloat(event.target.value));
  };

  const handlePowerSaveChange = (event) => {
    setPowerSave(event.target.value);
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

        {/* Power Management Settings */}
        <Box
          sx={{
            marginTop: 2,
            border: 2,
            borderColor: "primary.main",
            borderRadius: 1,
            padding: 2,
          }}
        >
          <Box
            sx={{
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              gap: 1,
              marginBottom: 2,
            }}
          >
            <BoltIcon sx={{ color: "primary.main" }} />
            <Typography
              variant="h6"
              sx={{
                margin: 0,
                color: "primary.main",
              }}
            >
              Power Management
            </Typography>
          </Box>

          <FormControl fullWidth variant="outlined">
            <InputLabel id="tx-power-label">Transmit Power (dBm)</InputLabel>
            <Select
              labelId="tx-power-label"
              value={txPower}
              onChange={handleTxPowerChange}
              label="Transmit Power (dBm)"
            >
              <MenuItem value={13}>13 dBm (Low - saves power)</MenuItem>
              <MenuItem value={15}>15 dBm (Medium-Low)</MenuItem>
              <MenuItem value={17}>17 dBm (Medium)</MenuItem>
              <MenuItem value={18}>18 dBm (Medium-High)</MenuItem>
              <MenuItem value={19.5}>19.5 dBm (High - default)</MenuItem>
              <MenuItem value={20}>
                20 dBm (Maximum - regulatory limit)
              </MenuItem>
              <MenuItem
                value={21}
                sx={{
                  color: "error.main",
                  fontWeight: "bold",
                }}
              >
                ⚠️ 21 dBm (ULTRA MODE - ILLEGAL/DANGEROUS)
              </MenuItem>
            </Select>
          </FormControl>

          {/* Ultra Mode Warning */}
          {txPower === 21 && (
            <Alert
              severity="error"
              sx={{
                marginTop: 2,
                fontWeight: "bold",
                "& .MuiAlert-message": {
                  width: "100%",
                },
              }}
            >
              <Typography variant="body2" sx={{ fontWeight: "bold" }}>
                ⚠️ ULTRA MODE ACTIVE - READ CAREFULLY:
              </Typography>
              <Typography
                variant="caption"
                component="div"
                sx={{ marginTop: 1 }}
              >
                • <strong>ILLEGAL</strong> in most countries (FCC/CE/ETSI
                violations)
              </Typography>
              <Typography variant="caption" component="div">
                • May cause <strong>RF interference</strong> to neighbors
              </Typography>
              <Typography variant="caption" component="div">
                • Can <strong>damage hardware</strong> and reduce lifespan
              </Typography>
              <Typography variant="caption" component="div">
                • Use <strong>ONLY</strong> in shielded lab environment
              </Typography>
              <Typography
                variant="caption"
                component="div"
                sx={{ marginTop: 1 }}
              >
                💡 For better range, use external antenna instead!
              </Typography>
              <Box
                sx={{
                  mt: 1.5,
                  display: "flex",
                  flexDirection: "column",
                  gap: 0.5,
                }}
              >
                <Typography variant="caption" sx={{ fontWeight: "bold" }}>
                  📚 Technical References:
                </Typography>
                <Link
                  href="https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf"
                  target="_blank"
                  rel="noopener noreferrer"
                  sx={{ fontSize: "0.7rem" }}
                >
                  ESP32 Datasheet (PA Capabilities)
                </Link>
                <Link
                  href="https://www.fcc.gov/engineering-technology/laboratory-division/general/equipment-authorization"
                  target="_blank"
                  rel="noopener noreferrer"
                  sx={{ fontSize: "0.7rem" }}
                >
                  FCC Regulations (USA)
                </Link>
                <Link
                  href="https://www.etsi.org/technologies/radio-equipment"
                  target="_blank"
                  rel="noopener noreferrer"
                  sx={{ fontSize: "0.7rem" }}
                >
                  ETSI/CE Regulations (Europe)
                </Link>
                <Link
                  href="https://en.wikipedia.org/wiki/Equivalent_isotropically_radiated_power"
                  target="_blank"
                  rel="noopener noreferrer"
                  sx={{ fontSize: "0.7rem" }}
                >
                  About EIRP (Effective Isotropic Radiated Power)
                </Link>
                <Typography
                  variant="caption"
                  sx={{ display: "block", mt: 0.5, fontStyle: "italic" }}
                >
                  💡 EIRP = TX Power + Antenna Gain - Cable Loss
                </Typography>
              </Box>
            </Alert>
          )}

          <FormControl fullWidth variant="outlined" sx={{ marginTop: 2 }}>
            <InputLabel id="power-save-label">Power Save Mode</InputLabel>
            <Select
              labelId="power-save-label"
              value={powerSave}
              onChange={handlePowerSaveChange}
              label="Power Save Mode"
            >
              <MenuItem value="none">None (Best Performance)</MenuItem>
              <MenuItem value="min">Minimum (Balanced)</MenuItem>
              <MenuItem value="max">Maximum (Lowest Power)</MenuItem>
            </Select>
          </FormControl>

          <Typography
            variant="caption"
            sx={{
              marginTop: 2,
              display: "block",
              color: "text.secondary",
              fontStyle: "italic",
            }}
          >
            💡 Lower transmit power reduces interference and power consumption.
            Power save modes primarily affect WiFi client mode and may increase
            latency.
          </Typography>
        </Box>
      </Box>
    </>
  );
}
