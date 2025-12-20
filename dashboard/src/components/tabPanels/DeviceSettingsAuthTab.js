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

export default function DeviceSettingsAuthTab({
  authEnabled,
  setAuthEnabled,
  username,
  setUsername,
  password,
  setPassword,
  showPassword,
  setShowPassword,
}) {
  const handleUsernameChange = (event) => {
    setUsername(event.target.value);
  };

  const handlePasswordChange = (event) => {
    setPassword(event.target.value);
  };

  const handleAuthToggle = (event) => {
    setAuthEnabled(event.target.checked);
  };

  const handleTogglePasswordVisibility = () => {
    setShowPassword((prev) => !prev);
  };

  return (
    <>
      <FormControl variant="outlined" sx={{ marginTop: 1 }}>
        <FormControlLabel
          control={<Switch checked={authEnabled} onChange={handleAuthToggle} />}
          label={
            authEnabled ? "Authentication Enabled" : "Authentication Disabled"
          }
        />
      </FormControl>

      {authEnabled && (
        <Box sx={{ marginTop: 2 }}>
          <FormControl fullWidth variant="outlined">
            <TextField
              label="Username"
              value={username}
              onChange={handleUsernameChange}
              variant="outlined"
              fullWidth
            />
            <TextField
              type={showPassword ? "text" : "password"}
              label="Password"
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
      )}
    </>
  );
}
