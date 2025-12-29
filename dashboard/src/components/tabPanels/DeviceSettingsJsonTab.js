import React from "react";
import { TextField, Alert, Box } from "@mui/material";

export default function DeviceSettingsJsonTab({
  jsonConfig,
  setJsonConfig,
  jsonError,
  setJsonError,
  isEditable,
  isMobile = false,
}) {
  const handleJsonChange = (event) => {
    setJsonConfig(event.target.value);
    if (jsonError) {
      setJsonError("");
    }
  };

  return (
    <Box
      sx={{
        display: "flex",
        flexDirection: "column",
        flex: 1,
        minHeight: 0,
      }}
    >
      {jsonError && (
        <Alert severity="error" sx={{ marginBottom: 2 }}>
          {jsonError}
        </Alert>
      )}
      <TextField
        label="Configuration JSON"
        value={jsonConfig}
        onChange={handleJsonChange}
        variant="outlined"
        fullWidth
        multiline
        minRows={8}
        error={!!jsonError}
        disabled={!isEditable}
        sx={{
          flex: 1,
          minHeight: 0,
          "& .MuiInputBase-input": {
            fontFamily: "monospace",
            fontSize: "0.875rem",
          },
          // Make the multiline input stretch to fill available height
          "& .MuiOutlinedInput-root": {
            alignItems: "stretch",
            height: "100%",
          },
          "& textarea": {
            height: "100% !important",
            overflow: "auto",
          },
        }}
      />
    </Box>
  );
}
