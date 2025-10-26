import React from "react";
import { TextField, Alert, Box } from "@mui/material";
import EditButton from "../EditButton";

export default function DeviceSettingsJsonTab({
  jsonConfig,
  setJsonConfig,
  jsonError,
  setJsonError,
  isEditable,
  toggleEditability,
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
      <Box sx={{ display: "flex", justifyContent: "flex-end", mb: 2 }}>
        <EditButton
          onClick={toggleEditability}
          tooltip={isEditable ? "Stop Editing" : "Edit"}
          isEditing={isEditable}
        />
      </Box>
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
