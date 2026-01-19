import React, { useMemo, useState, useEffect, useRef } from "react";
import Module from "./Module";
import {
  Box,
  Typography,
  IconButton,
  Tooltip,
  TextField,
  InputAdornment,
  List,
  ListItem,
  ListItemText,
  ListItemButton,
  CircularProgress,
  Slider,
} from "@mui/material";
import PlayArrowIcon from "@mui/icons-material/PlayArrow";
import PauseIcon from "@mui/icons-material/Pause";
import StopIcon from "@mui/icons-material/Stop";
import SkipNextIcon from "@mui/icons-material/SkipNext";
import SkipPreviousIcon from "@mui/icons-material/SkipPrevious";
import CastIcon from "@mui/icons-material/Cast";
import LibraryMusicIcon from "@mui/icons-material/LibraryMusic";
import SearchIcon from "@mui/icons-material/Search";
import CloseIcon from "@mui/icons-material/Close";
import VolumeUpIcon from "@mui/icons-material/VolumeUp";
import MusicPlayerSettingsModal from "./MusicPlayerSettingsModal";
import { buildWebSocketUrl } from "../utils/apiUtils";

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
  const [searchQuery, setSearchQuery] = useState("");
  const [settingsData, setSettingsData] = useState({
    musicDir: config?.musicDir || "/music",
  });
  const [duration, setDuration] = useState(0); // seconds (finite; may reflect seekable end)
  const [currentTime, setCurrentTime] = useState(0); // seconds
  const [seekPreviewTime, setSeekPreviewTime] = useState(null); // seconds or null
  const [volume, setVolume] = useState(0.7);
  const [isCasting, setIsCasting] = useState(false);
  const [showSearch, setShowSearch] = useState(false);

  const isMountedRef = useRef(true);
  const audioRef = useRef(null);
  const mediaWsRef = useRef(null);
  const mediaSourceRef = useRef(null);
  const sourceBufferRef = useRef(null);
  const objectUrlRef = useRef("");
  const pendingChunkLenRef = useRef(0);
  const streamActiveRef = useRef(false);
  const stopInProgressRef = useRef(false);
  const suppressAudioErrorRef = useRef(false);
  const streamSeqRef = useRef(0);
  const isSeekingRef = useRef(false);
  const pendingSeekTimeRef = useRef(null);
  const userPausedRef = useRef(false);

  // Supported audio file extensions
  const AUDIO_EXTENSIONS = [".mp3", ".wav", ".ogg", ".m4a", ".aac", ".flac"];

  const filteredMusicFiles = useMemo(() => {
    const q = (searchQuery || "").trim().toLowerCase();
    if (!q) return musicFiles;
    return (musicFiles || []).filter((f) =>
      String(f?.name || "")
        .toLowerCase()
        .includes(q)
    );
  }, [musicFiles, searchQuery]);

  const hasSearchFilter = Boolean((searchQuery || "").trim());

  // Update settings data when config changes
  useEffect(() => {
    setSettingsData({
      musicDir: config?.musicDir || "/music",
    });
  }, [config?.musicDir]);

  // Initialize audio element
  useEffect(() => {
    const audio = new Audio();
    audio.volume = volume;

    // Event listeners for audio element
    audio.addEventListener("loadedmetadata", () => {
      const d = Number(audio.duration);
      if (Number.isFinite(d) && d > 0) {
        setDuration(d);
      }
    });

    audio.addEventListener("timeupdate", () => {
      if (!isSeekingRef.current) {
        const t = Number(audio.currentTime);
        if (Number.isFinite(t)) {
          setCurrentTime(t);
        }

        // Some streaming modes report duration=Infinity; use seekable end instead.
        let maxT = Number(audio.duration);
        if (!Number.isFinite(maxT) || maxT <= 0) {
          try {
            if (audio.seekable && audio.seekable.length > 0) {
              maxT = Number(audio.seekable.end(audio.seekable.length - 1));
            }
          } catch {
            // ignore
          }
        }
        if (Number.isFinite(maxT) && maxT > 0) {
          setDuration(maxT);
        }
      }
    });

    audio.addEventListener("ended", () => {
      // Auto-play next track
      handleSkipNext();
    });

    audio.addEventListener("error", (e) => {
      // Rapid track switching / intentional stop can trigger transient errors.
      if (suppressAudioErrorRef.current || stopInProgressRef.current) {
        return;
      }
      console.error("🎵 Audio error:", e);
      setIsPlaying(false);
      setIsPaused(false);
    });

    audio.addEventListener("play", () => {
      setIsPlaying(true);
      setIsPaused(false);
      userPausedRef.current = false;
    });

    audio.addEventListener("pause", () => {
      // Ignore pauses triggered by teardown/track switching
      if (stopInProgressRef.current) {
        return;
      }
      userPausedRef.current = true;
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
    if (!controlWs || controlWs.readyState !== WebSocket.OPEN) {
      throw new Error("WebSocket not connected");
    }

    if (typeof controlWs.sendCommand !== "function") {
      throw new Error("WebSocket sendCommand method not available");
    }

    return await controlWs.sendCommand(cmd, 10000);
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
      console.log("🎵 Loaded music files");
    } catch (error) {
      console.warn("🎵 Error loading music files:", error.message || error);
      setMusicFiles([]);
    } finally {
      setLoading(false);
    }
  };

  // Load music files on mount and when config changes
  // Add a small delay to ensure server is ready after WebSocket connection
  useEffect(() => {
    if (deviceOnline && controlWs && controlWs.readyState === WebSocket.OPEN) {
      // Delay slightly to ensure server is ready
      const timer = setTimeout(() => {
        loadMusicFiles();
      }, 500);
      return () => clearTimeout(timer);
    }
  }, [deviceOnline, config?.musicDir, controlWs]);

  // Handle track selection
  const handleSelectTrack = async (file, index) => {
    if (!audioRef.current) return;

    // Bump sequence so any in-flight WS/audio events from older streams are ignored.
    const mySeq = (streamSeqRef.current = streamSeqRef.current + 1);

    // Stop any existing playback/stream before switching tracks
    await handleStop();
    userPausedRef.current = false;

    const musicDir = config?.musicDir || "/music";
    const filePath = `${musicDir}/${file.name}`;

    console.log("🎵 Playing track:", file.name);

    // Update state immediately
    setCurrentTrack(file);
    setCurrentTrackIndex(index);
    setCurrentTime(0);

    // Build ws://.../ws/media URL (include token via apiUtils)
    const mdnsHostname = globalConfig?.hostname || globalConfig?.deviceName;
    const mediaWsUrl = buildWebSocketUrl("/ws/media", mdnsHostname || null);
    if (!mediaWsUrl) {
      console.error("🎵 Failed to build media WebSocket URL.");
      return;
    }

    // Setup websocket
    streamActiveRef.current = true;
    pendingChunkLenRef.current = 0;

    const ws = new WebSocket(mediaWsUrl);
    ws.binaryType = "arraybuffer";
    mediaWsRef.current = ws;

    // Guess MIME; server will also accept mime hint
    const lower = String(file.name || "").toLowerCase();
    const mime = lower.endsWith(".mp3")
      ? "audio/mpeg"
      : lower.endsWith(".ogg")
      ? "audio/ogg"
      : "audio/mpeg";

    // Decide playback pipeline:
    // - If MediaSource is available AND supports this mime, stream progressively.
    // - Otherwise, download full file over WS, then play from a Blob URL.
    const canMse =
      typeof window !== "undefined" &&
      "MediaSource" in window &&
      typeof window.MediaSource?.isTypeSupported === "function" &&
      window.MediaSource.isTypeSupported(mime);

    let ms = null;
    let sourceOpen = false;
    let startAck = null;
    const blobParts = [];

    const setAudioSrcObjectUrl = (url) => {
      if (!audioRef.current) return;
      audioRef.current.src = url;
      try {
        audioRef.current.load();
      } catch {
        // ignore
      }
    };

    if (canMse) {
      ms = new MediaSource();
      mediaSourceRef.current = ms;
      const objUrl = URL.createObjectURL(ms);
      objectUrlRef.current = objUrl;
      setAudioSrcObjectUrl(objUrl);

      const onSourceOpen = () => {
        sourceOpen = true;
        // If we already got music_start, we can init SB now.
        if (!startAck || sourceBufferRef.current) return;
        try {
          const sb = ms.addSourceBuffer(startAck?.mime || mime);
          sourceBufferRef.current = sb;
          sb.addEventListener("updateend", () => {
            // Pull next chunk after append completes
            if (streamActiveRef.current) {
              try {
                ws.send(JSON.stringify({ cmd: "music_next" }));
              } catch {
                // ignore
              }
            }
          });
          // Kick off first chunk
          try {
            ws.send(JSON.stringify({ cmd: "music_next" }));
          } catch {
            // ignore
          }
        } catch (e) {
          console.error("🎵 addSourceBuffer failed:", e);
          handleStop();
        }
      };

      // Attach immediately to avoid sourceopen race.
      ms.addEventListener("sourceopen", onSourceOpen, { once: true });
      if (ms.readyState === "open") {
        onSourceOpen();
      }
    } else {
      // Blob-download mode: don't set src until we have full file.
      mediaSourceRef.current = null;
      sourceBufferRef.current = null;
      objectUrlRef.current = "";
      setAudioSrcObjectUrl("");
    }

    const startCmd = {
      cmd: "music_start",
      fs: "sd",
      path: filePath,
      mime,
      chunkSize: 16384,
    };

    ws.onopen = () => {
      if (mySeq !== streamSeqRef.current) {
        try {
          ws.close(1000, "superseded");
        } catch {
          // ignore
        }
        return;
      }
      try {
        ws.send(JSON.stringify(startCmd));
      } catch (e) {
        console.error("🎵 Failed to send music_start:", e);
      }
    };

    ws.onmessage = async (event) => {
      if (mySeq !== streamSeqRef.current) {
        return;
      }
      // Text: control/metadata; Binary: audio bytes
      if (typeof event.data === "string") {
        let msg;
        try {
          msg = JSON.parse(event.data);
        } catch {
          return;
        }

        if (msg?.type === "music_start") {
          if (msg?.ok === false) {
            console.error("🎵 music_start failed:", msg?.error || "unknown");
            handleStop();
            return;
          }
          startAck = msg;

          // Kick off streaming in blob mode immediately.
          if (!canMse) {
            try {
              ws.send(JSON.stringify({ cmd: "music_next" }));
            } catch {
              // ignore
            }
          } else {
            // If source is already open, init SB now.
            if (sourceOpen && ms && !sourceBufferRef.current) {
              try {
                const sb = ms.addSourceBuffer(startAck?.mime || mime);
                sourceBufferRef.current = sb;
                sb.addEventListener("updateend", () => {
                  if (streamActiveRef.current) {
                    try {
                      ws.send(JSON.stringify({ cmd: "music_next" }));
                    } catch {
                      // ignore
                    }
                  }
                });
                try {
                  ws.send(JSON.stringify({ cmd: "music_next" }));
                } catch {
                  // ignore
                }
              } catch (e) {
                console.error("🎵 addSourceBuffer failed:", e);
                handleStop();
              }
            }
          }
        }

        if (msg?.type === "music_chunk") {
          if (msg?.eof) {
            if (canMse && ms) {
              try {
                if (ms.readyState === "open") ms.endOfStream();
              } catch {
                // ignore
              }
            } else {
              // Blob-download complete: create a playable URL and start playback.
              try {
                const blob = new Blob(blobParts, {
                  type: startAck?.mime || mime,
                });
                const url = URL.createObjectURL(blob);
                objectUrlRef.current = url;
                setAudioSrcObjectUrl(url);
                if (!userPausedRef.current) {
                  await audioRef.current.play();
                }
              } catch (e) {
                console.error("🎵 Failed to play downloaded track:", e);
              }
              // Close stream after completion
              try {
                ws.close(1000, "music_eof");
              } catch {
                // ignore
              }
            }
            return;
          }
          pendingChunkLenRef.current = Number(msg?.len || 0);
        }

        return;
      }

      if (event.data instanceof ArrayBuffer) {
        if (mySeq !== streamSeqRef.current) {
          return;
        }
        const expected = pendingChunkLenRef.current;
        if (!expected || expected <= 0) {
          return;
        }

        const buf = new Uint8Array(event.data);
        pendingChunkLenRef.current = 0;

        if (canMse) {
          const sb = sourceBufferRef.current;
          if (!sb) return;

          // Wait until sourceBuffer is ready
          const append = () => {
            try {
              if (!sb.updating) {
                sb.appendBuffer(buf);
              } else {
                setTimeout(append, 10);
              }
            } catch (e) {
              console.error("🎵 appendBuffer failed:", e);
            }
          };
          append();

          // Start playback as soon as we have some buffered data
          if (!userPausedRef.current && audioRef.current?.paused) {
            try {
              await audioRef.current.play();
            } catch {
              // ignore autoplay restrictions; user initiated click should allow
            }
          }
        } else {
          // Blob-download mode: buffer bytes, then request next chunk.
          blobParts.push(buf);
          try {
            ws.send(JSON.stringify({ cmd: "music_next" }));
          } catch {
            // ignore
          }
        }
      }
    };

    ws.onerror = (e) => {
      // Don't spam errors when we're intentionally stopping/switching.
      if (mySeq !== streamSeqRef.current || stopInProgressRef.current) {
        return;
      }
      console.warn("🎵 media socket error:", e);
      handleStop();
    };

    ws.onclose = () => {
      // When switching tracks we intentionally supersede old sockets.
      if (mySeq !== streamSeqRef.current || stopInProgressRef.current) {
        return;
      }
      // Unexpected close: stop playback.
      if (streamActiveRef.current) {
        handleStop();
      }
    };
  };

  // Handle play/pause
  const handlePlayPause = async () => {
    if (!audioRef.current) return;

    // If no track selected, play the first one
    if (!currentTrack && musicFiles.length > 0) {
      handleSelectTrack(musicFiles[0], 0);
      return;
    }

    // Toggle play/pause
    if (isPlaying && !isPaused) {
      audioRef.current.pause();
    } else {
      try {
        await audioRef.current.play();
      } catch (error) {
        console.error("🎵 Error playing audio:", error);
      }
    }
  };

  // Handle stop
  const handleStop = async () => {
    if (!audioRef.current) return;

    stopInProgressRef.current = true;
    suppressAudioErrorRef.current = true;
    setTimeout(() => {
      suppressAudioErrorRef.current = false;
    }, 250);

    try {
      // Stop media stream first (best-effort)
      streamActiveRef.current = false;
      if (mediaWsRef.current) {
        const ws = mediaWsRef.current;
        mediaWsRef.current = null;

        // Avoid browser console noise: don't close while CONNECTING.
        if (ws.readyState === WebSocket.CONNECTING) {
          ws.onopen = () => {
            try {
              ws.send(JSON.stringify({ cmd: "music_stop" }));
            } catch {
              // ignore
            }
            try {
              ws.close(1000, "stop_after_connect");
            } catch {
              // ignore
            }
          };
        } else {
          try {
            if (ws.readyState === WebSocket.OPEN) {
              ws.send(JSON.stringify({ cmd: "music_stop" }));
            }
          } catch {
            // ignore
          }
          try {
            ws.close(1000, "stop");
          } catch {
            // ignore
          }
        }
      }
    } finally {
      // Tear down MediaSource / blob URL
      sourceBufferRef.current = null;
      mediaSourceRef.current = null;
      pendingChunkLenRef.current = 0;
      if (objectUrlRef.current && objectUrlRef.current.startsWith("blob:")) {
        try {
          URL.revokeObjectURL(objectUrlRef.current);
        } catch {
          // ignore
        }
        objectUrlRef.current = "";
      }

      // Stop local audio playback and abort network requests
      try {
        audioRef.current.pause();
      } catch {
        // ignore
      }
      try {
        audioRef.current.currentTime = 0;
      } catch {
        // ignore
      }

      // Clear the source to abort ongoing requests
      try {
        audioRef.current.src = "";
        audioRef.current.load(); // Reset the audio element
      } catch {
        // ignore
      }

      setCurrentTime(0);
      setIsPlaying(false);
      setIsPaused(false);
      stopInProgressRef.current = false;
    }
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

    if (isCasting) {
      console.log(
        "🎵 Note: Volume control for cast devices must be adjusted on the device itself"
      );
    }
  };

  // Scrub/seek handlers
  const clampToSeekable = (t) => {
    const a = audioRef.current;
    if (!a) return t;
    try {
      if (a.seekable && a.seekable.length > 0) {
        const start = a.seekable.start(0);
        const end = a.seekable.end(a.seekable.length - 1);
        if (Number.isFinite(start) && Number.isFinite(end) && end >= start) {
          if (t < start) return start;
          if (t > end) return end;
        }
      }
    } catch {
      // ignore
    }
    // Fallback clamp to [0, duration]
    if (duration && Number.isFinite(duration)) {
      if (t < 0) return 0;
      if (t > duration) return duration;
    }
    return t;
  };

  const applySeek = (t) => {
    const a = audioRef.current;
    if (!a) return false;
    const tt = clampToSeekable(t);
    try {
      if (typeof a.fastSeek === "function") {
        a.fastSeek(tt);
      } else {
        a.currentTime = tt;
      }
      pendingSeekTimeRef.current = null;
      return true;
    } catch {
      // Some browsers throw if not seekable yet; retry on canplay/loadedmetadata.
      pendingSeekTimeRef.current = tt;
      return false;
    }
  };

  const handleSeekChange = (event, newValue) => {
    if (!audioRef.current) return;
    isSeekingRef.current = true;
    const t = Array.isArray(newValue) ? newValue[0] : newValue;
    const tt = Number(t);
    if (!Number.isFinite(tt)) return;
    setSeekPreviewTime(tt);
    setCurrentTime(tt);
  };

  const handleSeekCommit = (event, newValue) => {
    if (!audioRef.current) return;
    const t = Array.isArray(newValue) ? newValue[0] : newValue;
    const tt = Number(t);
    if (!Number.isFinite(tt)) return;
    applySeek(tt);
    isSeekingRef.current = false;
    setSeekPreviewTime(null);
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
        title={
          <Box
            component="span"
            sx={{
              display: "inline-flex",
              alignItems: "center",
              justifyContent: "center",
              gap: 0.5,
              maxWidth: "100%",
            }}
          >
            <LibraryMusicIcon
              sx={{
                fontSize: { xs: 28, sm: 32 },
                color: "primary.main",
                flex: "0 0 auto",
              }}
            />

            {/* Search: anchor stays fixed; field expands without reflow */}
            <Box
              component="span"
              sx={{
                position: "relative",
                display: "inline-flex",
                alignItems: "center",
                justifyContent: "center",
                width: { xs: 34, sm: 40 },
                height: { xs: 34, sm: 40 },
              }}
            >
              <Tooltip title="Search songs">
                <span>
                  <IconButton
                    onClick={() => setShowSearch(true)}
                    size="small"
                    sx={{
                      color: hasSearchFilter
                        ? "secondary.main"
                        : "primary.main",
                      padding: { xs: "4px", sm: 1 },
                      minWidth: 0,
                      opacity: showSearch ? 0 : 1,
                      pointerEvents: showSearch ? "none" : "auto",
                      transition: "opacity 120ms ease",
                    }}
                  >
                    <SearchIcon sx={{ fontSize: { xs: 18, sm: 22 } }} />
                  </IconButton>
                </span>
              </Tooltip>

              <Box
                component="span"
                sx={{
                  position: "absolute",
                  left: 0,
                  top: "50%",
                  transform: "translateY(-50%)",
                  overflow: "hidden",
                  width: showSearch ? { xs: 170, sm: 230, md: 280 } : 0,
                  opacity: showSearch ? 1 : 0,
                  pointerEvents: showSearch ? "auto" : "none",
                  transition:
                    "width 240ms cubic-bezier(0.2, 0, 0, 1), opacity 120ms ease",
                  willChange: "width, opacity",
                }}
              >
                <TextField
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  placeholder="Search…"
                  size="small"
                  autoComplete="off"
                  sx={{
                    width: { xs: 170, sm: 230, md: 280 },
                    "& .MuiInputBase-root": {
                      fontSize: { xs: "0.8rem", sm: "0.85rem" },
                    },
                  }}
                  InputProps={{
                    endAdornment: (
                      <InputAdornment position="end">
                        <Tooltip title="Close search">
                          <span>
                            <IconButton
                              size="small"
                              aria-label="Close search"
                              onClick={() => {
                                setShowSearch(false);
                                setSearchQuery("");
                              }}
                              sx={{
                                color: hasSearchFilter
                                  ? "secondary.main"
                                  : "primary.main",
                              }}
                            >
                              <CloseIcon fontSize="small" />
                            </IconButton>
                          </span>
                        </Tooltip>
                      </InputAdornment>
                    ),
                  }}
                />
              </Box>
            </Box>
          </Box>
        }
        onSettings={handleOpenSettings}
        settingsDisabled={!deviceOnline}
        settingsTooltip="Settings"
        errorOutline={!deviceOnline && !isPlaying}
        sx={{
          width: { xs: "100%", sm: "auto" },
          minWidth: { xs: "320px", sm: "520px" },
          maxWidth: { xs: "100%", sm: "820px" },
          minHeight: "auto",
          maxHeight: { xs: "90vh", sm: "720px" },
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
            // Let the list consume remaining space in taller layouts.
            minHeight: { xs: "auto", sm: 560 },
          }}
        >
          {/* Current track display */}
          <Box
            sx={{
              width: "100%",
              minHeight: { xs: "34px", sm: "54px" },
              display: "flex",
              flexDirection: "column",
              alignItems: "center",
              justifyContent: "center",
              // Single cohesive playback control box background
              backgroundColor: "rgba(0, 0, 0, 0.12)",
              borderRadius: 1,
              padding: { xs: "6px", sm: 1.25 },
              position: "relative",
            }}
          >
            <Typography
              variant="body2"
              align="center"
              sx={{
                fontWeight: "bold",
                fontSize: { xs: "0.65rem", sm: "0.8rem" },
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
            {/* (status text removed; times are shown next to scrub bar) */}

            {/* Controls + timeline layout: transport centered over the scrub bar */}
            <Box
              sx={{
                width: "100%",
                mt: 0.25,
                px: { xs: 0.5, sm: 1 },
                display: "grid",
                gridTemplateColumns: {
                  xs: "auto 1fr auto",
                  sm: "auto 1fr auto auto",
                },
                gridTemplateRows: { xs: "auto auto auto", sm: "auto auto" },
                alignItems: "center",
                columnGap: { xs: 0.6, sm: 0.8 },
                rowGap: { xs: 0.5, sm: 0.25 },
              }}
            >
              {/* Transport row (centered over scrub column) */}
              <Box
                sx={{
                  gridRow: 1,
                  gridColumn: { xs: "1 / -1", sm: 2 },
                  justifySelf: "center",
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  gap: { xs: 0.5, sm: 1 },
                }}
              >
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
                      <SkipPreviousIcon sx={{ fontSize: { xs: 18, sm: 22 } }} />
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
                        <PauseIcon sx={{ fontSize: { xs: 28, sm: 36 } }} />
                      ) : (
                        <PlayArrowIcon sx={{ fontSize: { xs: 28, sm: 36 } }} />
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
                      <StopIcon sx={{ fontSize: { xs: 18, sm: 22 } }} />
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
                      <SkipNextIcon sx={{ fontSize: { xs: 18, sm: 22 } }} />
                    </IconButton>
                  </span>
                </Tooltip>
              </Box>

              {/* Timeline row */}
              <Typography
                variant="caption"
                color="text.secondary"
                sx={{
                  gridRow: 2,
                  gridColumn: 1,
                  fontSize: { xs: "0.55rem", sm: "0.7rem" },
                  minWidth: { xs: 28, sm: 36 },
                  textAlign: "left",
                  opacity: duration > 0 ? 1 : 0.6,
                  fontVariantNumeric: "tabular-nums",
                }}
              >
                {formatTime(
                  isSeekingRef.current && seekPreviewTime !== null
                    ? seekPreviewTime
                    : currentTime
                )}
              </Typography>

              <Slider
                value={
                  isSeekingRef.current && seekPreviewTime !== null
                    ? seekPreviewTime
                    : currentTime
                }
                onChange={handleSeekChange}
                onChangeCommitted={handleSeekCommit}
                min={0}
                max={duration > 0 ? duration : 0}
                step={0.25}
                size="small"
                disabled={!currentTrack || !(duration > 0)}
                sx={{
                  gridRow: 2,
                  gridColumn: 2,
                  width: "100%",
                  minWidth: 0,
                  "& .MuiSlider-thumb": {
                    width: { xs: 10, sm: 12 },
                    height: { xs: 10, sm: 12 },
                  },
                  "& .MuiSlider-track": {
                    height: { xs: 2, sm: 3 },
                  },
                  "& .MuiSlider-rail": {
                    height: { xs: 2, sm: 3 },
                    opacity: 0.3,
                  },
                }}
              />

              <Typography
                variant="caption"
                color="text.secondary"
                sx={{
                  gridRow: 2,
                  gridColumn: 3,
                  fontSize: { xs: "0.55rem", sm: "0.7rem" },
                  minWidth: { xs: 28, sm: 36 },
                  textAlign: "right",
                  opacity: duration > 0 ? 1 : 0.6,
                  fontVariantNumeric: "tabular-nums",
                }}
              >
                {formatTime(duration)}
              </Typography>

              {/* Right side: volume then cast (kept in-frame) */}
              <Box
                sx={{
                  // Move up into the transport row on desktop; keep below on mobile
                  gridRow: { xs: 3, sm: 1 },
                  gridColumn: { xs: "1 / -1", sm: 4 },
                  justifySelf: { xs: "end", sm: "end" },
                  display: "flex",
                  alignItems: "center",
                  gap: 0.75,
                  opacity: isCasting ? 0.5 : 1,
                }}
              >
                {/* Own box/pill */}
                <Box
                  sx={{
                    display: "flex",
                    alignItems: "center",
                    gap: 0.5,
                    minWidth: 0,
                    // Match the playback control box background
                    backgroundColor: "rgba(0, 0, 0, 0.12)",
                    borderRadius: 1,
                    px: { xs: 0.5, sm: 0.75 },
                    py: { xs: 0.25, sm: 0.35 },
                    // Nudge down slightly (was lifted up too much)
                    transform: { xs: "none", sm: "translateY(-1px)" },
                  }}
                >
                  <Tooltip
                    title={
                      isCasting
                        ? "Casting (click to disconnect)"
                        : "Cast to Device"
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
                          padding: { xs: "4px", sm: 0.75 },
                          minWidth: 0,
                        }}
                      >
                        <CastIcon sx={{ fontSize: { xs: 18, sm: 22 } }} />
                      </IconButton>
                    </span>
                  </Tooltip>

                  <VolumeUpIcon sx={{ fontSize: { xs: 18, sm: 20 } }} />
                  <Slider
                    value={volume * 100}
                    onChange={handleVolumeChange}
                    disabled={isCasting}
                    min={0}
                    max={100}
                    step={1}
                    size="small"
                    color="secondary"
                    sx={{
                      width: { xs: 150, sm: 120, md: 150 },
                      maxWidth: "100%",
                      "& .MuiSlider-thumb": {
                        width: { xs: 10, sm: 12 },
                        height: { xs: 10, sm: 12 },
                      },
                      "& .MuiSlider-track": {
                        height: { xs: 2, sm: 3 },
                      },
                      "& .MuiSlider-rail": {
                        height: { xs: 2, sm: 3 },
                        opacity: 0.3,
                      },
                    }}
                  />
                </Box>
              </Box>
            </Box>
          </Box>

          {/* Track list */}
          <Box
            sx={{
              width: "100%",
              maxWidth: "100%",
              maxHeight: { xs: "240px", sm: "420px" },
              overflow: "auto",
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
              flex: { xs: "0 0 auto", sm: "1 1 auto" },
              minHeight: { xs: "200px", sm: "260px" },
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
            ) : filteredMusicFiles.length === 0 ? (
              <Box sx={{ padding: { xs: 1, sm: 2 }, textAlign: "center" }}>
                <Typography
                  variant="body2"
                  color="text.secondary"
                  sx={{ fontSize: { xs: "0.7rem", sm: "0.875rem" } }}
                >
                  {musicFiles.length === 0
                    ? `No music files found in ${config?.musicDir || "/music"}`
                    : "No matches"}
                </Typography>
              </Box>
            ) : (
              <List dense sx={{ py: { xs: 0, sm: 1 }, width: "100%" }}>
                {filteredMusicFiles.map((file) => (
                  <ListItem
                    key={file?.path || file?.name}
                    disablePadding
                    sx={{ width: "100%" }}
                  >
                    <ListItemButton
                      selected={currentTrack?.path === file?.path}
                      onClick={() =>
                        handleSelectTrack(
                          file,
                          musicFiles.findIndex((f) => f?.path === file?.path)
                        )
                      }
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
