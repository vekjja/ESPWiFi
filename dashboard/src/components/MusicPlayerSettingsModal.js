import React from "react";
import {
  Button,
  TextField,
  FormControl,
  InputLabel,
  Divider,
  Box,
} from "@mui/material";
import SettingsModal from "./SettingsModal";
import LibraryMusicIcon from "@mui/icons-material/LibraryMusic";
import DeleteButton from "./DeleteButton";

export default function MusicPlayerSettingsModal({
  open,
  onClose,
  onSave,
  onDelete,
  musicPlayerData,
  onMusicPlayerDataChange,
}) {
  const handleChange = (field, value) => {
    onMusicPlayerDataChange({
      ...musicPlayerData,
      [field]: value,
    });
  };

  const handleSave = () => {
    // Validate required fields
    if (!musicPlayerData.musicDir || musicPlayerData.musicDir.trim() === "") {
      alert("Please enter a music directory path");
      return;
    }

    // Ensure music directory starts with /
    let musicDir = musicPlayerData.musicDir.trim();
    if (!musicDir.startsWith("/")) {
      musicDir = "/" + musicDir;
    }

    onSave({
      ...musicPlayerData,
      musicDir,
    });
  };

  const handleDelete = () => {
    if (onDelete) {
      onDelete();
    }
  };

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <span
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: "8px",
            width: "100%",
          }}
        >
          <LibraryMusicIcon color="primary" />
          Music Player Settings
        </span>
      }
      actions={
        <>
          <Button onClick={onClose} color="inherit">
            Cancel
          </Button>
          <Button onClick={handleSave} color="primary" variant="contained">
            Save
          </Button>
        </>
      }
    >
      <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
        {/* Music Directory */}
        <TextField
          label="Music Directory"
          value={musicPlayerData.musicDir || ""}
          onChange={(e) => handleChange("musicDir", e.target.value)}
          fullWidth
          placeholder="/music"
          helperText="Path to the music folder on the SD card (e.g., /music)"
        />

        {/* Information */}
        <Box
          sx={{
            padding: 2,
            backgroundColor: "rgba(0, 0, 0, 0.1)",
            borderRadius: 1,
          }}
        >
          <InputLabel sx={{ fontSize: "0.875rem", color: "text.secondary" }}>
            Supported Audio Formats
          </InputLabel>
          <Box sx={{ marginTop: 1 }}>
            <span style={{ fontSize: "0.875rem", color: "text.secondary" }}>
              MP3, WAV, OGG, M4A, AAC, FLAC
            </span>
          </Box>
        </Box>

        {/* Delete Module Section */}
        {onDelete && (
          <>
            <Divider sx={{ marginTop: 2, marginBottom: 1 }} />
            <FormControl fullWidth>
              <Box
                sx={{
                  display: "flex",
                  flexDirection: "column",
                  alignItems: "center",
                  gap: 1,
                }}
              >
                <InputLabel sx={{ fontSize: "0.875rem", marginBottom: 1 }}>
                  Remove this music player module
                </InputLabel>
                <DeleteButton
                  onClick={handleDelete}
                  tooltip="Delete Music Player Module"
                  confirmMessage="Are you sure you want to delete this music player module?"
                />
              </Box>
            </FormControl>
          </>
        )}
      </Box>
    </SettingsModal>
  );
}
