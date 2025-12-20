import React, { useState } from "react";
import { Box, TextField, Button, Alert, CircularProgress } from "@mui/material";
import { buildApiUrl } from "../utils/apiUtils";
import { setAuthToken } from "../utils/authUtils";
import SettingsModal from "./SettingsModal";

const Login = ({ onLoginSuccess }) => {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);

  const handleSubmit = async (e) => {
    e.preventDefault();
    setError(null);
    setLoading(true);

    try {
      const response = await fetch(buildApiUrl("/api/auth/login"), {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ username, password }),
      });

      const data = await response.json();

      if (!response.ok) {
        throw new Error(data.error || "Login failed");
      }

      // If auth is disabled, token will be empty string
      if (data.token) {
        setAuthToken(data.token);
        onLoginSuccess();
      } else {
        // Auth is disabled, allow access
        onLoginSuccess();
      }
    } catch (err) {
      setError(err.message || "Failed to login. Please try again.");
      setLoading(false);
    }
  };

  return (
    <SettingsModal
      open={true}
      onClose={() => {}} // Prevent closing - user must login
      disableEscapeKeyDown={true} // Prevent ESC key from closing
      title="ESPWiFi Login"
      maxWidth="sm"
      actions={
        <Button
          type="submit"
          form="login-form"
          variant="contained"
          disabled={loading}
          fullWidth
        >
          {loading ? <CircularProgress size={24} /> : "Login"}
        </Button>
      }
    >
      <Box
        component="form"
        id="login-form"
        onSubmit={handleSubmit}
        sx={{ display: "flex", flexDirection: "column", gap: 2 }}
      >
        {error && (
          <Alert severity="error" onClose={() => setError(null)}>
            {error}
          </Alert>
        )}

        <TextField
          label="Username"
          variant="outlined"
          fullWidth
          value={username}
          onChange={(e) => setUsername(e.target.value)}
          required
          disabled={loading}
          autoComplete="username"
        />
        <TextField
          label="Password"
          type="password"
          variant="outlined"
          fullWidth
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          required
          disabled={loading}
          autoComplete="current-password"
        />
      </Box>
    </SettingsModal>
  );
};

export default Login;
