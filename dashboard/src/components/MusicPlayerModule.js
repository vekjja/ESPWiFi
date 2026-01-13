import React, { useState, useEffect, useRef } from "react";
import Module from "./Module";
import {
  Box,
  Typography,
  IconButton,
  Tooltip,
  List,
  ListItem,
  ListItemText,
  ListItemButton,
  CircularProgress,
  LinearProgress,
  Slider,
} from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import StopIcon from "@mui/icons-material/Stop";
import SkipNextIcon from "@mui/icons-material/SkipNext";
import SkipPreviousIcon from "@mui/icons-material/SkipPrevious";
import QueueMusicIcon from "@mui/icons-material/QueueMusic";
import VolumeUpIcon from "@mui/icons-material/VolumeUp";
import MusicPlayerSettingsModal from "./MusicPlayerSettingsModal";
import { buildApiUrl } from "../utils/apiUtils";
import { getAuthToken } from "../utils/authUtils";

export default function MusicPlayerModule({
  config,
  globalConfig,
  onUpdate,
  onDelete,
  deviceOnline = true,
  saveConfigToDevice,
  controlWs,
  onMusicPlaybackChange,
}) {
  const moduleKey = config?.key;
  const [musicFiles, setMusicFiles] = useState([]);
  const [currentTrack, setCurrentTrack] = useState(null);
  const [currentTrackIndex, setCurrentTrackIndex] = useState(-1);
  const [isPlaying, setIsPlaying] = useState(false);
  const [isPaused, setIsPaused] = useState(false);
  const [loading, setLoading] = useState(false);
  const [settingsModalOpen, setSettingsModalOpen] = useState(false);
  const [settingsData, setSettingsData] = useState({
    name: config?.name || "Music Player",
    musicDir: config?.musicDir || "/music",
  });
  const [progress, setProgress] = useState(0);
  const [duration, setDuration] = useState(0);
  const [currentTime, setCurrentTime] = useState(0);
  const [volume, setVolume] = useState(0.7);

  const isMountedRef = useRef(true);
  const audioRef = useRef(null);

  // Supported audio file extensions
  const AUDIO_EXTENSIONS = [".mp3", ".wav", ".ogg", ".m4a", ".aac", ".flac"];

  // Update settings data when config changes
  useEffect(() => {
    setSettingsData({
      name: config?.name || "Music Player",
      musicDir: config?.musicDir || "/music",
    });
  }, [config?.name, config?.musicDir]);

  // Initialize audio element
  useEffect(() => {
    const audio = new Audio();
    audio.volume = volume;

    // Event listeners for audio element
    audio.addEventListener("loadedmetadata", () => {
      setDuration(audio.duration);
    });

    audio.addEventListener("timeupdate", () => {
      setCurrentTime(audio.currentTime);
      if (audio.duration > 0) {
        setProgress((audio.currentTime / audio.duration) * 100);
      }
    });

    audio.addEventListener("ended", () => {
      // Auto-play next track
      handleSkipNext();
    });

    audio.addEventListener("error", (e) => {
      console.error("🎵 Audio error:", e);
      setIsPlaying(false);
      setIsPaused(false);
    });

    audio.addEventListener("play", () => {
      setIsPlaying(true);
      setIsPaused(false);
    });

    audio.addEventListener("pause", () => {
      setIsPaused(true);
    });

    audioRef.current = audio;

    return () => {
      audio.pause();
      audio.src = "";
      audio.remove();
    };
  }, []);

  // Helper to send WebSocket command and wait for response
  const sendWsCommand = async (cmd) => {
    return new Promise((resolve, reject) => {
      if (!controlWs || controlWs.readyState !== WebSocket.OPEN) {
        reject(new Error("WebSocket not connected"));
        return;
      }

      const timeout = setTimeout(() => {
        reject(new Error("WebSocket command timed out"));
      }, 10000);

      const handleMessage = (event) => {
        try {
          const response = JSON.parse(event.data);
          if (response.cmd === cmd.cmd) {
            clearTimeout(timeout);
            controlWs.removeEventListener("message", handleMessage);
            if (response.ok === false) {
              reject(new Error(response.error || "Command failed"));
            } else {
              resolve(response);
            }
          }
        } catch (err) {
          // Ignore parse errors for other messages
        }
      };

      controlWs.addEventListener("message", handleMessage);
      controlWs.send(JSON.stringify(cmd));
    });
  };

  // Load music files from SD card using WebSocket
  const loadMusicFiles = async () => {
    if (
      !deviceOnline ||
      !controlWs ||
      controlWs.readyState !== WebSocket.OPEN
    ) {
      console.log(
        "🎵 Cannot load files: device offline or WebSocket not connected"
      );
      return;
    }

    setLoading(true);
    try {
      const musicDir = config?.musicDir || "/music";

      // Use WebSocket to list files (like FileBrowser does)
      const response = await sendWsCommand({
        cmd: "list_files",
        fs: "sd", // SD card file system
        path: musicDir,
      });

      // Filter for audio files only
      const audioFiles = (response.files || []).filter(
        (file) =>
          !file.isDirectory &&
          AUDIO_EXTENSIONS.some((ext) => file.name.toLowerCase().endsWith(ext))
      );

      setMusicFiles(audioFiles);
      console.log("🎵 Loaded music files:", audioFiles);
    } catch (error) {
      console.error("🎵 Error loading music files:", error);
      setMusicFiles([]);
    } finally {
      setLoading(false);
    }
  };

  // Load music files on mount and when config changes
  useEffect(() => {
    if (deviceOnline && controlWs && controlWs.readyState === WebSocket.OPEN) {
      loadMusicFiles();
    }
  }, [deviceOnline, config?.musicDir, controlWs]);

  // Handle track selection
  const handleSelectTrack = async (file, index) => {
    if (!audioRef.current) return;

    const musicDir = config?.musicDir || "/music";
    const filePath = `${musicDir}/${file.name}`;

    // Build URL like FileBrowser does:
    // Encode the file path properly - split by '/' and encode each segment
    const mdnsHostname = globalConfig?.hostname || globalConfig?.deviceName;
    const encodedPath = filePath
      .split("/")
      .map((segment) => encodeURIComponent(segment))
      .join("/");

    // Use /sd prefix for SD card file system (like FileBrowser)
    let fileUrl = buildApiUrl(`/sd${encodedPath}`, mdnsHostname);

    // Add auth token as query parameter
    const token = getAuthToken();
    if (
      token &&
      token !== "null" &&
      token !== "undefined" &&
      token.trim() !== ""
    ) {
      const sep = fileUrl.includes("?") ? "&" : "?";
      fileUrl = `${fileUrl}${sep}token=${encodeURIComponent(token)}`;
    }

    console.log("🎵 Playing track:", file.name, "URL:", fileUrl);

    // Set audio source and play
    audioRef.current.src = fileUrl;

    try {
      await audioRef.current.play();
      setCurrentTrack(file);
      setCurrentTrackIndex(index);
      setIsPlaying(true);
      setIsPaused(false);
      setProgress(0);
    } catch (error) {
      console.error("🎵 Error playing track:", error);
    }
  };

  // Handle play/pause
  const handlePlayPause = async () => {
    if (!audioRef.current) return;

    if (!currentTrack) {
      // If no track selected, play the first one
      if (musicFiles.length > 0) {
        handleSelectTrack(musicFiles[0], 0);
      }
      return;
    }

    if (isPlaying && !isPaused) {
      // Pause
      audioRef.current.pause();
    } else {
      // Play/Resume
      try {
        await audioRef.current.play();
      } catch (error) {
        console.error("🎵 Error playing audio:", error);
      }
    }
  };

  // Handle stop
  const handleStop = () => {
    if (!audioRef.current) return;

    audioRef.current.pause();
    audioRef.current.currentTime = 0;
    setIsPlaying(false);
    setIsPaused(false);
    setProgress(0);
    setCurrentTime(0);
  };

  // Handle skip next
  const handleSkipNext = () => {
    if (musicFiles.length === 0) return;

    const nextIndex = (currentTrackIndex + 1) % musicFiles.length;
    handleSelectTrack(musicFiles[nextIndex], nextIndex);
  };

  // Handle skip previous
  const handleSkipPrevious = () => {
    if (musicFiles.length === 0) return;

    const prevIndex =
      currentTrackIndex <= 0 ? musicFiles.length - 1 : currentTrackIndex - 1;
    handleSelectTrack(musicFiles[prevIndex], prevIndex);
  };

  // Handle volume change
  const handleVolumeChange = (event, newValue) => {
    const newVolume = newValue / 100;
    setVolume(newVolume);
    if (audioRef.current) {
      audioRef.current.volume = newVolume;
    }
  };

  // Handle settings
  const handleOpenSettings = () => {
    if (!globalConfig || !saveConfigToDevice) {
      console.warn("Cannot open music player settings - config not loaded yet");
      return;
    }
    setSettingsModalOpen(true);
  };

  const handleCloseSettings = () => {
    setSettingsModalOpen(false);
  };

  const handleSaveSettings = (updatedData) => {
    if (onUpdate && moduleKey && updatedData) {
      onUpdate(moduleKey, {
        name: updatedData.name,
        musicDir: updatedData.musicDir,
      });
      // Reload music files with new directory
      loadMusicFiles();
    }
    handleCloseSettings();
  };

  const handleDeleteModule = () => {
    console.log("🗑️ Music player module delete initiated", { moduleKey });
    handleStop();
    handleCloseSettings();
    if (onDelete && moduleKey !== null && moduleKey !== undefined) {
      onDelete(moduleKey);
    }
  };

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      isMountedRef.current = false;
      if (audioRef.current) {
        audioRef.current.pause();
        audioRef.current.src = "";
      }
      // Reset playback state on unmount
      if (onMusicPlaybackChange) {
        onMusicPlaybackChange({ isPlaying: false, isPaused: false });
      }
    };
  }, [onMusicPlaybackChange]);

  // Notify parent of playback state changes
  useEffect(() => {
    if (onMusicPlaybackChange) {
      onMusicPlaybackChange({ isPlaying, isPaused });
    }
  }, [isPlaying, isPaused, onMusicPlaybackChange]);

  // Format time in MM:SS
  const formatTime = (seconds) => {
    if (!seconds || isNaN(seconds) || !isFinite(seconds)) return "0:00";
    const mins = Math.floor(seconds / 60);
    const secs = Math.floor(seconds % 60);
    return `${mins}:${secs.toString().padStart(2, "0")}`;
  };

  // Format file size
  const formatFileSize = (bytes) => {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    return (bytes / (1024 * 1024)).toFixed(1) + " MB";
  };

  // Get border color based on state
  const getBorderColor = () => {
    // If music is playing, the device is clearly online (serving files)
    // even if WebSocket control connection is down
    if (isPlaying && isPaused) {
      return "musicPlayer.paused"; // Orange when paused
    }
    if (isPlaying) {
      return "musicPlayer.playing"; // Cyan when playing (device must be online!)
    }
    // Only show error/offline state when NOT playing
    if (!deviceOnline && !isPlaying) {
      return "error.main"; // Red when offline and not playing
    }
    return "primary.main"; // Default primary color
  };

  return (
    <>
      <Module
        title={config?.name || "Music Player"}
        onSettings={handleOpenSettings}
        settingsDisabled={!deviceOnline}
        settingsTooltip="Settings"
        errorOutline={!deviceOnline && !isPlaying}
        sx={{
          minWidth: "400px",
          maxWidth: "500px",
          minHeight: "auto",
          maxHeight: "600px",
          borderColor: getBorderColor(),
          "& .MuiCardContent-root": {
            minHeight: "auto",
            paddingBottom: "0px !important",
          },
        }}
      >
        <Box
          sx={{
            width: "100%",
            display: "flex",
            flexDirection: "column",
            gap: 1,
          }}
        >
          {/* Current track display */}
          <Box
            sx={{
              width: "100%",
              minHeight: "80px",
              display: "flex",
              flexDirection: "column",
              alignItems: "center",
              justifyContent: "center",
              backgroundColor: "rgba(0, 0, 0, 0.2)",
              borderRadius: 1,
              padding: 2,
              position: "relative",
            }}
          >
            <QueueMusicIcon
              sx={{ fontSize: 32, color: "primary.main", mb: 1 }}
            />
            <Typography
              variant="body2"
              align="center"
              sx={{ fontWeight: "bold" }}
            >
              {currentTrack ? currentTrack.name : "No track selected"}
            </Typography>
            {isPlaying && (
              <Typography variant="caption" color="text.secondary">
                {isPaused ? "Paused" : "Playing"} • {formatTime(currentTime)} /{" "}
                {formatTime(duration)}
              </Typography>
            )}

            {/* Progress bar */}
            {isPlaying && (
              <LinearProgress
                variant="determinate"
                value={progress}
                sx={{ width: "100%", mt: 1 }}
              />
            )}

            {/* Status indicator */}
            <Box
              sx={{
                position: "absolute",
                top: 8,
                right: 8,
                width: 8,
                height: 8,
                borderRadius: "50%",
                backgroundColor: !deviceOnline
                  ? "error.main"
                  : isPlaying && !isPaused
                  ? "success.main"
                  : "text.disabled",
              }}
            />
          </Box>

          {/* Playback controls */}
          <Box
            sx={{
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              gap: 1,
              padding: 1,
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
            }}
          >
            <Tooltip
              title={!deviceOnline ? "Device Offline" : "Previous Track"}
            >
              <span>
                <IconButton
                  onClick={handleSkipPrevious}
                  disabled={musicFiles.length === 0 || !currentTrack}
                  sx={{
                    color:
                      musicFiles.length === 0 || !currentTrack
                        ? "text.disabled"
                        : "primary.main",
                  }}
                >
                  <SkipPreviousIcon />
                </IconButton>
              </span>
            </Tooltip>

            <Tooltip title={isPlaying && !isPaused ? "Pause" : "Play"}>
              <span>
                <IconButton
                  onClick={handlePlayPause}
                  disabled={musicFiles.length === 0}
                  sx={{
                    color:
                      musicFiles.length === 0
                        ? "text.disabled"
                        : "primary.main",
                    fontSize: 40,
                  }}
                >
                  {isPlaying && !isPaused ? (
                    <PauseIcon fontSize="large" />
                  ) : (
                    <PlayArrowIcon fontSize="large" />
                  )}
                </IconButton>
              </span>
            </Tooltip>

            <Tooltip title="Stop">
              <span>
                <IconButton
                  onClick={handleStop}
                  disabled={!isPlaying}
                  sx={{
                    color: !isPlaying ? "text.disabled" : "error.main",
                  }}
                >
                  <StopIcon />
                </IconButton>
              </span>
            </Tooltip>

            <Tooltip title="Next Track">
              <span>
                <IconButton
                  onClick={handleSkipNext}
                  disabled={musicFiles.length === 0 || !currentTrack}
                  sx={{
                    color:
                      musicFiles.length === 0 || !currentTrack
                        ? "text.disabled"
                        : "primary.main",
                  }}
                >
                  <SkipNextIcon />
                </IconButton>
              </span>
            </Tooltip>
          </Box>

          {/* Volume Control */}
          <Box
            sx={{
              display: "flex",
              alignItems: "center",
              gap: 1,
              padding: 1,
              paddingLeft: 2,
              paddingRight: 2,
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
            }}
          >
            <VolumeUpIcon sx={{ color: "primary.main", fontSize: 20 }} />
            <Slider
              value={volume * 100}
              onChange={handleVolumeChange}
              min={0}
              max={100}
              step={1}
              sx={{
                flex: 1,
                "& .MuiSlider-thumb": {
                  width: 12,
                  height: 12,
                },
              }}
            />
            <Typography
              variant="caption"
              sx={{ minWidth: 35, textAlign: "right" }}
            >
              {Math.round(volume * 100)}%
            </Typography>
          </Box>

          {/* Track list */}
          <Box
            sx={{
              width: "100%",
              maxHeight: "300px",
              overflow: "auto",
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
            }}
          >
            {loading ? (
              <Box
                sx={{
                  display: "flex",
                  justifyContent: "center",
                  alignItems: "center",
                  padding: 4,
                }}
              >
                <CircularProgress size={24} />
              </Box>
            ) : musicFiles.length === 0 ? (
              <Box sx={{ padding: 2, textAlign: "center" }}>
                <Typography variant="body2" color="text.secondary">
                  No music files found in {config?.musicDir || "/music"}
                </Typography>
              </Box>
            ) : (
              <List dense>
                {musicFiles.map((file, index) => (
                  <ListItem key={index} disablePadding>
                    <ListItemButton
                      selected={currentTrackIndex === index}
                      onClick={() => handleSelectTrack(file, index)}
                    >
                      <ListItemText
                        primary={file.name}
                        secondary={formatFileSize(file.size)}
                        primaryTypographyProps={{
                          sx: {
                            fontSize: "0.875rem",
                            overflow: "hidden",
                            textOverflow: "ellipsis",
                            whiteSpace: "nowrap",
                          },
                        }}
                        secondaryTypographyProps={{
                          sx: { fontSize: "0.75rem" },
                        }}
                      />
                    </ListItemButton>
                  </ListItem>
                ))}
              </List>
            )}
          </Box>
        </Box>
      </Module>

      <MusicPlayerSettingsModal
        open={settingsModalOpen && globalConfig && saveConfigToDevice}
        onClose={handleCloseSettings}
        onSave={handleSaveSettings}
        onDelete={handleDeleteModule}
        musicPlayerData={settingsData}
        onMusicPlayerDataChange={setSettingsData}
        config={globalConfig}
        saveConfigToDevice={saveConfigToDevice}
        moduleConfig={config}
        onModuleUpdate={onUpdate}
      />
    </>
  );
}
