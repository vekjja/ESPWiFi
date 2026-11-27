import React from "react";
import {
  Button,
  TextField,
  FormControl,
  InputLabel,
  Divider,
  Box,
} from "@mui/material";
import SettingsModal from "../../common/SettingsModal";
import MovieIcon from "@mui/icons-material/Movie";
import DeleteButton from "../../common/DeleteButton";

export default function MoviePlayerSettingsModal({
  open,
  onClose,
  onSave,
  onDelete,
  movieData,
  onMovieDataChange,
}) {
  const handleChange = (field, value) => {
    onMovieDataChange({
      ...movieData,
      [field]: value,
    });
  };

  const handleSave = () => {
    if (!movieData.movieDir || movieData.movieDir.trim() === "") {
      alert("Please enter a movie directory path");
      return;
    }
    let movieDir = movieData.movieDir.trim();
    if (!movieDir.startsWith("/")) movieDir = "/" + movieDir;
    onSave({ ...movieData, movieDir });
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
          <MovieIcon color="primary" />
          Movie Player Settings
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
        <TextField
          label="Movie Directory"
          value={movieData.movieDir || ""}
          onChange={(e) => handleChange("movieDir", e.target.value)}
          fullWidth
          placeholder="/movies"
          helperText="Path to the movie folder on the SD card (e.g., /movies)"
        />
        <Box
          sx={{
            padding: 2,
            backgroundColor: "rgba(0, 0, 0, 0.1)",
            borderRadius: 1,
          }}
        >
          <InputLabel sx={{ fontSize: "0.875rem", color: "text.secondary" }}>
            Supported Movie Formats
          </InputLabel>
          <Box sx={{ marginTop: 1 }}>
            <span style={{ fontSize: "0.875rem", color: "text.secondary" }}>
              MP4, MOV, WebM, and MKV. The browser decodes H.264, VP8, VP9, and
              AV1 video, with AAC, Opus, or FLAC audio. Other audio, such as
              AC-3 or DTS, is skipped.
            </span>
          </Box>
        </Box>
        {onDelete && (
          <>
            <Divider sx={{ marginTop: 2, marginBottom: 1 }} />
            <FormControl fullWidth>
              <Box sx={{ display: "flex", justifyContent: "center" }}>
                <DeleteButton onClick={onDelete} tooltip="Delete Movie Player" />
              </Box>
            </FormControl>
          </>
        )}
      </Box>
    </SettingsModal>
  );
}
