import React, { useState } from "react";
import { TextField, Fab, Tooltip, Alert } from "@mui/material";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";
import SaveIcon from "@mui/icons-material/Save";
import SaveAsIcon from "@mui/icons-material/SaveAs";
import EditIcon from "@mui/icons-material/Edit"; // Correct the import to use the existing Edit icon

export default function ConfigButton({ config, saveConfig }) {
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [jsonConfig, setJsonConfig] = useState("");
  const [jsonError, setJsonError] = useState("");
  const [isEditable, setIsEditable] = useState(false); // State to control editability

  const handleOpenModal = () => {
    // Format the config as pretty JSON when opening the modal
    setJsonConfig(JSON.stringify(config, null, 2));
    setJsonError("");
    setIsModalOpen(true);
  };

  const handleCloseModal = () => {
    setIsModalOpen(false);
    setJsonError("");
  };

  const handleSave = () => {
    try {
      // Parse the JSON to validate it
      const parsedConfig = JSON.parse(jsonConfig);

      // Basic validation - ensure required fields exist
      if (!parsedConfig.mdns) {
        setJsonError("Configuration must include 'mdns' field");
        return;
      }

      setJsonError("");
      saveConfig(parsedConfig);
      handleCloseModal();
    } catch (error) {
      setJsonError("Invalid JSON format. Please check your configuration.");
    }
  };

  const handleJsonChange = (event) => {
    setJsonConfig(event.target.value);
    // Clear error when user starts typing
    if (jsonError) {
      setJsonError("");
    }
  };

  const toggleEditability = () => {
    setIsEditable((prev) => !prev);
  };

  return (
    <>
      <Tooltip title="Configuration">
        <Fab
          size="small"
          color="primary"
          onClick={handleOpenModal}
          sx={{
            position: "fixed",
            top: "20px",
            left: "72px", // Position to the right of the NetworkSettings button
            zIndex: 1001,
          }}
        >
          <SaveAsIcon />
        </Fab>
      </Tooltip>

      <SettingsModal
        open={isModalOpen}
        onClose={handleCloseModal}
        title="Configuration"
        maxWidth="lg"
        actions={
          <>
            <IButton
              color={isEditable ? "secondary" : "default"}
              Icon={EditIcon} // Use the corrected Edit icon
              onClick={toggleEditability}
              tooltip={isEditable ? "Disable Editing" : "Enable Editing"}
            />
            <IButton
              color="primary"
              Icon={SaveIcon}
              onClick={handleSave}
              tooltip="Save Configuration to Device"
            />
          </>
        }
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
          rows={20}
          error={!!jsonError}
          disabled={!isEditable} // Disable editing if not editable
          sx={{
            marginTop: 2,
            "& .MuiInputBase-input": {
              fontFamily: "monospace",
              fontSize: "0.875rem",
            },
          }}
        />
      </SettingsModal>
    </>
  );
}
