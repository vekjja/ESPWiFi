/**
 * @file AuthInfoCard.js
 * @brief Authentication settings card
 * 
 * Displays and allows editing of authentication configuration
 */

import React, { useState, useEffect } from "react";
import {
  Box,
  Typography,
  Chip,
  TextField,
  Button,
  FormControlLabel,
  Switch,
  InputAdornment,
  IconButton,
} from "@mui/material";
import KeyIcon from "@mui/icons-material/Key";
import CheckCircleIcon from "@mui/icons-material/CheckCircle";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";

/**
 * AuthInfoCard Component
 * 
 * @param {Object} props - Component props
 * @param {Object} props.config - Device configuration
 * @param {Function} props.onSave - Callback to save configuration
 * @returns {JSX.Element} The rendered auth card
 */
export default function AuthInfoCard({ config, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [tempAuthEnabled, setTempAuthEnabled] = useState(false);
  const [tempAuthUsername, setTempAuthUsername] = useState("");
  const [tempAuthPassword, setTempAuthPassword] = useState("");
  const [showAuthPassword, setShowAuthPassword] = useState(false);

  useEffect(() => {
    if (config) {
      setTempAuthEnabled(config.auth?.enabled || false);
      setTempAuthUsername(config.auth?.username || "");
      setTempAuthPassword(config.auth?.password || "");
    }
  }, [config]);

  const handleSave = () => {
    const authConfig = {
      auth: {
        ...config.auth,
        enabled: tempAuthEnabled,
        username: tempAuthUsername,
        password: tempAuthPassword,
      },
    };
    onSave(authConfig);
    setIsEditing(false);
  };

  const handleCancel = () => {
    setTempAuthEnabled(config?.auth?.enabled || false);
    setTempAuthUsername(config?.auth?.username || "");
    setTempAuthPassword(config?.auth?.password || "");
    setIsEditing(false);
  };

  const viewContent = (
    <>
      <Box sx={{ display: "flex", justifyContent: "space-between" }}>
        <Typography variant="body2" color="text.secondary">
          Status:
        </Typography>
        <Chip
          icon={config?.auth?.enabled ? <CheckCircleIcon /> : undefined}
          label={config?.auth?.enabled ? "Enabled" : "Disabled"}
          color={config?.auth?.enabled ? "success" : "default"}
          size="small"
        />
      </Box>
      {config?.auth?.enabled && (
        <>
          <InfoRow label="Username:" value={config.auth.username || "N/A"} />
          <InfoRow label="Password:" value="••••••••" />
        </>
      )}
    </>
  );

  const editContent = (
    <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
      <FormControlLabel
        control={
          <Switch
            checked={tempAuthEnabled}
            onChange={(e) => setTempAuthEnabled(e.target.checked)}
            color="primary"
          />
        }
        label="Enable Authentication"
      />

      {tempAuthEnabled && (
        <>
          <TextField
            fullWidth
            size="small"
            label="Username"
            value={tempAuthUsername}
            onChange={(e) => setTempAuthUsername(e.target.value)}
            placeholder="admin"
            helperText="Username for web interface access"
          />
          <TextField
            fullWidth
            size="small"
            label="Password"
            type={showAuthPassword ? "text" : "password"}
            value={tempAuthPassword}
            onChange={(e) => setTempAuthPassword(e.target.value)}
            placeholder="Enter password"
            helperText="Password for web interface access"
            InputProps={{
              endAdornment: (
                <InputAdornment position="end">
                  <IconButton
                    onClick={() => setShowAuthPassword(!showAuthPassword)}
                    edge="end"
                    size="small"
                  >
                    {showAuthPassword ? (
                      <VisibilityOffIcon />
                    ) : (
                      <VisibilityIcon />
                    )}
                  </IconButton>
                </InputAdornment>
              ),
            }}
          />
        </>
      )}

      <Box sx={{ display: "flex", gap: 1, justifyContent: "flex-end" }}>
        <Button
          variant="outlined"
          size="small"
          startIcon={<CancelIcon />}
          onClick={handleCancel}
        >
          Cancel
        </Button>
        <Button
          variant="contained"
          size="small"
          startIcon={<SaveIcon />}
          onClick={handleSave}
        >
          Save Auth
        </Button>
      </Box>
    </Box>
  );

  return (
    <InfoCard
      title="Authentication"
      icon={KeyIcon}
      editable
      isEditing={isEditing}
      onEdit={() => setIsEditing(true)}
      editContent={editContent}
    >
      {viewContent}
    </InfoCard>
  );
}

