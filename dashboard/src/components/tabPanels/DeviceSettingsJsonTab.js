import React from "react";
import { TextField, Alert } from "@mui/material";

export default function DeviceSettingsJsonTab({
  jsonConfig,
  setJsonConfig,
  jsonError,
  setJsonError,
  isEditable,
}) {
  const handleJsonChange = (event) => {
    setJsonConfig(event.target.value);
    if (jsonError) {
      setJsonError("");
    }
  };

  return (
    <>
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
        rows={20}
        error={!!jsonError}
        disabled={!isEditable}
        sx={{
          "& .MuiInputBase-input": {
            fontFamily: "monospace",
            fontSize: "0.875rem",
          },
        }}
      />
    </>
  );
}
