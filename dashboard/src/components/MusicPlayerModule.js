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
import CastIcon from "@mui/icons-material/Cast";
import ShareIcon from "@mui/icons-material/Share";
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
  const [isCasting, setIsCasting] = useState(false);

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

    // Remote Playback API listeners (for cast status)
    if ("remote" in audio) {
      const handleRemoteStateChange = () => {
        const state = audio.remote.state;
        setIsCasting(state === "connected");
        console.log("🎵 Cast state:", state);
      };

      audio.remote.addEventListener("connect", handleRemoteStateChange);
      audio.remote.addEventListener("connecting", handleRemoteStateChange);
      audio.remote.addEventListener("disconnect", handleRemoteStateChange);
    }

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

    // Inform user if casting that volume control is on the cast device
    if (isCasting) {
      console.log(
        "🎵 Note: Volume control for cast devices must be adjusted on the device itself"
      );
    }
  };

  // Handle cast to external device (Chromecast, etc.)
  const handleCast = async () => {
    if (!audioRef.current || !currentTrack) {
      alert("Please select and play a track before casting.");
      return;
    }

    // Check if Remote Playback API is available
    if (!("remote" in audioRef.current)) {
      alert(
        "Casting is not supported by this browser. Try using Chrome or Edge on desktop."
      );
      return;
    }

    try {
      const remote = audioRef.current.remote;

      // Check if remote playback is available
      if (remote.state === "disconnected") {
        console.log("🎵 Opening cast device picker...");
        await remote.prompt();
        console.log("🎵 Cast connected successfully");
      } else if (remote.state === "connecting") {
        alert("Cast is connecting, please wait...");
      } else if (remote.state === "connected") {
        alert("Already casting! Use your cast device to control playback.");
      }
    } catch (error) {
      console.error("🎵 Cast error:", error);

      // Provide more specific error messages
      if (error.name === "NotSupportedError") {
        alert(
          "This audio format or source is not supported for casting. Try a different track or use the share button to play on another device."
        );
      } else if (error.name === "AbortError") {
        console.log("🎵 Cast selection cancelled by user");
      } else if (error.name === "InvalidStateError") {
        alert("Please start playing the track before attempting to cast.");
      } else if (error.name === "NotAllowedError") {
        alert("Casting was blocked. Check your browser permissions.");
      } else {
        alert(
          `Unable to cast: ${
            error.message || "Unknown error"
          }. The audio source may require authentication that cast devices cannot provide.`
        );
      }
    }
  };

  // Handle share (copy track URL to clipboard)
  const handleShare = async () => {
    if (!currentTrack) return;

    const filePath = `${settingsData.musicDir}/${currentTrack.name}`;
    const pathSegments = filePath.split("/").filter((s) => s);
    const encodedPath = pathSegments.map(encodeURIComponent).join("/");
    const mdnsHostname = globalConfig?.mdns_hostname || "espwifi";

    const fileUrl = buildApiUrl(`/sd/${encodedPath}`, mdnsHostname);
    const token = getAuthToken();
    const fullUrl = token ? `${fileUrl}?token=${token}` : fileUrl;

    try {
      // Try to use Web Share API if available
      if (navigator.share) {
        await navigator.share({
          title: currentTrack.name,
          text: `Listen to ${currentTrack.name}`,
          url: fullUrl,
        });
        console.log("🎵 Shared successfully");
      } else {
        // Fallback to clipboard
        await navigator.clipboard.writeText(fullUrl);
        alert("Track URL copied to clipboard!");
      }
    } catch (error) {
      console.error("🎵 Share error:", error);
      // Final fallback - try clipboard even if share failed
      try {
        await navigator.clipboard.writeText(fullUrl);
        alert("Track URL copied to clipboard!");
      } catch (clipError) {
        alert("Unable to share or copy URL");
      }
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
          width: { xs: "100%", sm: "auto" },
          minWidth: { xs: "280px", sm: "400px" },
          maxWidth: { xs: "100%", sm: "500px" },
          minHeight: "auto",
          maxHeight: { xs: "90vh", sm: "600px" },
          borderColor: getBorderColor(),
          "& .MuiCardContent-root": {
            minHeight: "auto",
            paddingBottom: "0px !important",
            padding: { xs: "8px !important", sm: 2 },
          },
        }}
      >
        <Box
          sx={{
            width: "100%",
            display: "flex",
            flexDirection: "column",
            gap: { xs: 0.5, sm: 1 },
            overflow: { xs: "hidden", sm: "visible" },
          }}
        >
          {/* Current track display */}
          <Box
            sx={{
              width: "100%",
              minHeight: { xs: "50px", sm: "80px" },
              display: "flex",
              flexDirection: "column",
              alignItems: "center",
              justifyContent: "center",
              backgroundColor: "rgba(0, 0, 0, 0.2)",
              borderRadius: 1,
              padding: { xs: "6px", sm: 2 },
              position: "relative",
            }}
          >
            <QueueMusicIcon
              sx={{
                fontSize: { xs: 20, sm: 32 },
                color: "primary.main",
                mb: { xs: 0.25, sm: 1 },
                display: { xs: "none", sm: "block" },
              }}
            />
            <Typography
              variant="body2"
              align="center"
              sx={{
                fontWeight: "bold",
                fontSize: { xs: "0.7rem", sm: "0.875rem" },
                px: { xs: 0.5, sm: 1 },
                lineHeight: { xs: 1.2, sm: 1.5 },
                maxWidth: "100%",
                overflow: "hidden",
                textOverflow: "ellipsis",
                whiteSpace: "nowrap",
              }}
            >
              {currentTrack ? currentTrack.name : "No track selected"}
            </Typography>
            {isPlaying && (
              <Typography
                variant="caption"
                color="text.secondary"
                sx={{ fontSize: { xs: "0.6rem", sm: "0.75rem" } }}
              >
                {isPaused ? "Paused" : "Playing"} • {formatTime(currentTime)} /{" "}
                {formatTime(duration)}
              </Typography>
            )}

            {/* Progress bar */}
            {isPlaying && (
              <LinearProgress
                variant="determinate"
                value={progress}
                sx={{ width: "100%", mt: { xs: 0.5, sm: 1 } }}
              />
            )}

            {/* Status indicator */}
            <Box
              sx={{
                position: "absolute",
                top: { xs: 4, sm: 8 },
                right: { xs: 4, sm: 8 },
                width: { xs: 6, sm: 8 },
                height: { xs: 6, sm: 8 },
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
              flexWrap: { xs: "nowrap", sm: "nowrap" },
              gap: { xs: 0.25, sm: 1 },
              padding: { xs: "4px", sm: 1 },
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
              overflow: "hidden",
            }}
          >
            <Tooltip
              title={
                isCasting ? "Casting (click to disconnect)" : "Cast to Device"
              }
            >
              <span>
                <IconButton
                  onClick={handleCast}
                  disabled={!currentTrack}
                  size="small"
                  sx={{
                    color: !currentTrack
                      ? "text.disabled"
                      : isCasting
                      ? "success.main"
                      : "primary.main",
                    padding: { xs: "4px", sm: 1 },
                    minWidth: 0,
                  }}
                >
                  <CastIcon sx={{ fontSize: { xs: 18, sm: 24 } }} />
                </IconButton>
              </span>
            </Tooltip>

            <Tooltip title="Share Track">
              <span>
                <IconButton
                  onClick={handleShare}
                  disabled={!currentTrack}
                  size="small"
                  sx={{
                    color: !currentTrack ? "text.disabled" : "primary.main",
                    padding: { xs: "4px", sm: 1 },
                    minWidth: 0,
                  }}
                >
                  <ShareIcon sx={{ fontSize: { xs: 18, sm: 24 } }} />
                </IconButton>
              </span>
            </Tooltip>

            <Tooltip
              title={!deviceOnline ? "Device Offline" : "Previous Track"}
            >
              <span>
                <IconButton
                  onClick={handleSkipPrevious}
                  disabled={musicFiles.length === 0 || !currentTrack}
                  size="small"
                  sx={{
                    color:
                      musicFiles.length === 0 || !currentTrack
                        ? "text.disabled"
                        : "primary.main",
                    padding: { xs: "4px", sm: 1 },
                    minWidth: 0,
                  }}
                >
                  <SkipPreviousIcon sx={{ fontSize: { xs: 18, sm: 24 } }} />
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
                    padding: { xs: "6px", sm: 1 },
                    minWidth: 0,
                  }}
                >
                  {isPlaying && !isPaused ? (
                    <PauseIcon sx={{ fontSize: { xs: 28, sm: 40 } }} />
                  ) : (
                    <PlayArrowIcon sx={{ fontSize: { xs: 28, sm: 40 } }} />
                  )}
                </IconButton>
              </span>
            </Tooltip>

            <Tooltip title="Stop">
              <span>
                <IconButton
                  onClick={handleStop}
                  disabled={!isPlaying}
                  size="small"
                  sx={{
                    color: !isPlaying ? "text.disabled" : "error.main",
                    padding: { xs: "4px", sm: 1 },
                    minWidth: 0,
                  }}
                >
                  <StopIcon sx={{ fontSize: { xs: 18, sm: 24 } }} />
                </IconButton>
              </span>
            </Tooltip>

            <Tooltip title="Next Track">
              <span>
                <IconButton
                  onClick={handleSkipNext}
                  disabled={musicFiles.length === 0 || !currentTrack}
                  size="small"
                  sx={{
                    color:
                      musicFiles.length === 0 || !currentTrack
                        ? "text.disabled"
                        : "primary.main",
                    padding: { xs: "4px", sm: 1 },
                    minWidth: 0,
                  }}
                >
                  <SkipNextIcon sx={{ fontSize: { xs: 18, sm: 24 } }} />
                </IconButton>
              </span>
            </Tooltip>
          </Box>

          {/* Volume Control */}
          <Box
            sx={{
              display: "flex",
              alignItems: "center",
              gap: { xs: 0.5, sm: 1 },
              padding: { xs: "4px 8px", sm: 1 },
              paddingLeft: { xs: 1, sm: 2 },
              paddingRight: { xs: 1, sm: 2 },
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
              position: "relative",
            }}
          >
            <Tooltip
              title={isCasting ? "Cast device controls volume" : "Volume"}
            >
              <VolumeUpIcon
                sx={{
                  color: isCasting ? "text.disabled" : "primary.main",
                  fontSize: { xs: 14, sm: 20 },
                }}
              />
            </Tooltip>
            <Slider
              value={volume * 100}
              onChange={handleVolumeChange}
              disabled={isCasting}
              min={0}
              max={100}
              step={1}
              sx={{
                flex: 1,
                opacity: isCasting ? 0.5 : 1,
                "& .MuiSlider-thumb": {
                  width: { xs: 8, sm: 12 },
                  height: { xs: 8, sm: 12 },
                },
                "& .MuiSlider-track": {
                  height: { xs: 2, sm: 3 },
                },
                "& .MuiSlider-rail": {
                  height: { xs: 2, sm: 3 },
                },
              }}
            />
            <Typography
              variant="caption"
              sx={{
                minWidth: { xs: 28, sm: 35 },
                textAlign: "right",
                fontSize: { xs: "0.6rem", sm: "0.75rem" },
                opacity: isCasting ? 0.5 : 1,
              }}
            >
              {isCasting ? "N/A" : `${Math.round(volume * 100)}%`}
            </Typography>
          </Box>

          {/* Track list */}
          <Box
            sx={{
              width: "100%",
              maxWidth: "100%",
              maxHeight: { xs: "200px", sm: "300px" },
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
                  padding: { xs: 1.5, sm: 4 },
                }}
              >
                <CircularProgress size={20} />
              </Box>
            ) : musicFiles.length === 0 ? (
              <Box sx={{ padding: { xs: 1, sm: 2 }, textAlign: "center" }}>
                <Typography
                  variant="body2"
                  color="text.secondary"
                  sx={{ fontSize: { xs: "0.7rem", sm: "0.875rem" } }}
                >
                  No music files found in {config?.musicDir || "/music"}
                </Typography>
              </Box>
            ) : (
              <List dense sx={{ py: { xs: 0, sm: 1 }, width: "100%" }}>
                {musicFiles.map((file, index) => (
                  <ListItem key={index} disablePadding sx={{ width: "100%" }}>
                    <ListItemButton
                      selected={currentTrackIndex === index}
                      onClick={() => handleSelectTrack(file, index)}
                      sx={{
                        py: { xs: "4px", sm: 1 },
                        px: { xs: 1, sm: 2 },
                        minHeight: { xs: 36, sm: 48 },
                        width: "100%",
                        overflow: "hidden",
                      }}
                    >
                      <ListItemText
                        primary={file.name}
                        secondary={formatFileSize(file.size)}
                        sx={{
                          overflow: "hidden",
                          width: "100%",
                          flex: "1 1 auto",
                        }}
                        primaryTypographyProps={{
                          sx: {
                            fontSize: { xs: "0.7rem", sm: "0.875rem" },
                            lineHeight: { xs: 1.2, sm: 1.5 },
                            overflow: "hidden",
                            textOverflow: "ellipsis",
                            whiteSpace: "nowrap",
                            display: "block",
                            width: "100%",
                          },
                        }}
                        secondaryTypographyProps={{
                          sx: {
                            fontSize: { xs: "0.6rem", sm: "0.75rem" },
                            lineHeight: { xs: 1.2, sm: 1.5 },
                            overflow: "hidden",
                            textOverflow: "ellipsis",
                            whiteSpace: "nowrap",
                            display: "block",
                          },
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
