/**
 * @file JsonConfigCard.js
 * @brief JSON configuration editor card
 * 
 * Provides a raw JSON editor for advanced configuration editing
 */

import React, { useState, useEffect } from "react";
import {
  Box,
  Typography,
  TextField,
  Button,
  Chip,
  Card,
  CardContent,
  Grid,
  Alert,
} from "@mui/material";
import CodeIcon from "@mui/icons-material/Code";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import EditIcon from "@mui/icons-material/Edit";

/**
 * JsonConfigCard Component
 * 
 * @param {Object} props - Component props
 * @param {Object} props.config - Device configuration
 * @param {Function} props.onSave - Callback to save configuration
 * @returns {JSX.Element} The rendered JSON config card
 */
export default function JsonConfigCard({ config, onSave }) {
  const [isEditing, setIsEditing] = useState(false);
  const [tempJsonConfig, setTempJsonConfig] = useState("");
  const [jsonError, setJsonError] = useState("");

  useEffect(() => {
    if (config) {
      setTempJsonConfig(JSON.stringify(config, null, 2));
    }
  }, [config]);

  const handleSave = () => {
    try {
      const parsedConfig = JSON.parse(tempJsonConfig);
      onSave(parsedConfig);
      setJsonError("");
      setIsEditing(false);
    } catch (error) {
      setJsonError(`Invalid JSON: ${error.message}`);
    }
  };

  const handleCancel = () => {
    setTempJsonConfig(JSON.stringify(config, null, 2));
    setJsonError("");
    setIsEditing(false);
  };

  const handleJsonChange = (event) => {
    setTempJsonConfig(event.target.value);
    if (jsonError) {
      setJsonError("");
    }
  };

  return (
    <Grid item xs={12}>
      <Card>
        <CardContent>
          <Box
            sx={{
              display: "flex",
              justifyContent: "space-between",
              alignItems: "center",
              mb: 2,
            }}
          >
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
              <CodeIcon color="primary" />
              <Typography variant="h6">JSON Configuration</Typography>
            </Box>
            {!isEditing && (
              <Button
                variant="outlined"
                size="small"
                startIcon={<EditIcon />}
                onClick={() => setIsEditing(true)}
              >
                Edit JSON
              </Button>
            )}
          </Box>

          {jsonError && (
            <Alert severity="error" sx={{ mb: 2 }}>
              {jsonError}
            </Alert>
          )}

          <Box
            sx={{
              border: (theme) => `1px solid ${theme.palette.divider}`,
              borderRadius: 1,
              p: 1,
              height: "60vh",
              display: "flex",
              flexDirection: "column",
              alignItems: "flex-start",
            }}
          >
            <Box
              sx={{
                display: "flex",
                justifyContent: "space-between",
                alignItems: "center",
                width: "100%",
                mb: 1,
              }}
            >
              <Typography variant="subtitle2" color="text.secondary">
                Configuration JSON
              </Typography>
              <Chip
                label={isEditing ? "Editing" : "Read-only"}
                size="small"
                color={isEditing ? "warning" : "default"}
              />
            </Box>
            <TextField
              value={
                isEditing
                  ? tempJsonConfig
                  : config
                  ? JSON.stringify(config, null, 2)
                  : "Loading..."
              }
              onChange={handleJsonChange}
              variant="standard"
              fullWidth
              multiline
              error={!!jsonError}
              InputProps={{
                disableUnderline: true,
                readOnly: !isEditing,
              }}
              sx={{
                flexGrow: 1,
                "& .MuiInputBase-root": {
                  height: "100%",
                  overflowY: "auto",
                  alignItems: "flex-start",
                  px: 2,
                  py: 1.5,
                },
                "& .MuiInputBase-input": {
                  fontFamily: "monospace",
                  fontSize: "0.875rem",
                  lineHeight: 1.6,
                  height: "100% !important",
                  overflow: "auto !important",
                },
              }}
            />
          </Box>

          {isEditing && (
            <Box
              sx={{
                display: "flex",
                gap: 1,
                justifyContent: "flex-end",
                mt: 2,
              }}
            >
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
                Save JSON
              </Button>
            </Box>
          )}
        </CardContent>
      </Card>
    </Grid>
  );
}

