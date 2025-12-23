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
import KeyIcon from "@mui/icons-material/Key";
import KeyOffIcon from "@mui/icons-material/KeyOff";

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

  const handleAuthToggle = () => {
    setAuthEnabled(!authEnabled);
  };

  const handleTogglePasswordVisibility = () => {
    setShowPassword((prev) => !prev);
  };

  return (
    <>
      <Tooltip
        title={
          authEnabled
            ? "Click to disable Authentication"
            : "Click to enable Authentication"
        }
      >
        <Box
          onClick={handleAuthToggle}
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 1,
            marginTop: 2,
            marginBottom: 2,
            cursor: "pointer",
            "&:hover": {
              opacity: 0.8,
            },
          }}
        >
          <IconButton
            sx={{
              color: authEnabled ? "primary.main" : "text.disabled",
              pointerEvents: "none",
            }}
          >
            {authEnabled ? <KeyIcon /> : <KeyOffIcon />}
          </IconButton>
          <Typography
            variant="body1"
            sx={{
              color: authEnabled ? "primary.main" : "text.disabled",
              pointerEvents: "none",
            }}
          >
            {authEnabled ? "Authentication Enabled" : "Authentication Disabled"}
          </Typography>
        </Box>
      </Tooltip>

      {authEnabled && (
        <Box
          sx={{
            marginTop: 2,
            border: 2,
            borderColor: "primary.main",
            borderRadius: 1,
            padding: 2,
            transition: "border-color 0.3s ease",
          }}
        >
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
