import React, { useMemo, useState, useEffect, useRef } from "react";
import Module from "../Module";
import { moduleScrollbarSx } from "../moduleConfig";
import InfoCard from "../../common/InfoCard";
import { SECTIONS } from "../../common/SectionTitle";
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
import SearchIcon from "@mui/icons-material/Search";
import CloseIcon from "@mui/icons-material/Close";
import VolumeUpIcon from "@mui/icons-material/VolumeUp";
import MusicPlayerSettingsModal from "./MusicPlayerSettingsModal";
import { buildWebSocketUrl } from "../../../utils/api";
import {
  canUseMse,
  createPcmPlayer,
  estimateTrack,
  mimeForName,
  primeAudioElement,
  unlockContext,
} from "./streamAudio";

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
  const [duration, setDuration] = useState(0); // seconds
  const [trackMeta, setTrackMeta] = useState(null);
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
  const streamSeqRef = useRef(0);
  const awaitingRef = useRef(false);
  const eofRef = useRef(false);
  const discardRef = useRef(false);
  const modeRef = useRef("");
  const timeOriginRef = useRef(0);
  const reopenMseRef = useRef(null);
  const knownDurationRef = useRef(0);
  const fileSizeRef = useRef(0);
  const sawAudioRef = useRef(false);
  const pcmRef = useRef(null);
  const audioCtxRef = useRef(null);
  const gainRef = useRef(null);
  const volumeRef = useRef(0.7);
  const stopInProgressRef = useRef(false);
  const suppressAudioErrorRef = useRef(false);
  const isSeekingRef = useRef(false);
  const userPausedRef = useRef(false);
  volumeRef.current = volume;

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
    audio.preload = "auto";
    audio.playsInline = true;
    audio.setAttribute("playsinline", "true");
    audio.setAttribute("webkit-playsinline", "true");

    // Event listeners for audio element
    audio.addEventListener("loadedmetadata", () => {
      if (knownDurationRef.current > 0) return;
      const d = Number(audio.duration);
      if (Number.isFinite(d) && d > 0) setDuration(d);
    });

    audio.addEventListener("timeupdate", () => {
      if (modeRef.current === "pcm" || isSeekingRef.current) return;
      const t = timeOriginRef.current + Number(audio.currentTime);
      if (Number.isFinite(t)) setCurrentTime(t);
      if (knownDurationRef.current > 0) return;
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
      if (Number.isFinite(maxT) && maxT > 0) setDuration(maxT);
    });

    audio.addEventListener("ended", () => {
      if (modeRef.current === "pcm") return;
      if (discardRef.current || suppressAudioErrorRef.current) return;
      handleSkipNext();
    });

    audio.addEventListener("error", (e) => {
      // Rapid track switching / intentional stop can trigger transient errors.
      // The phone unlock clip also fires this and must not cancel the song.
      if (
        modeRef.current === "pcm" ||
        suppressAudioErrorRef.current ||
        stopInProgressRef.current
      ) {
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
      // The phone path plays a tiny silent clip during the tap. That clip
      // ending is not the user pausing the song.
      if (
        modeRef.current === "pcm" ||
        stopInProgressRef.current ||
        suppressAudioErrorRef.current
      ) {
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
      streamActiveRef.current = false;
      if (pcmRef.current) pcmRef.current.stop();
      if (mediaWsRef.current) {
        try {
          mediaWsRef.current.close();
        } catch {
          // ignore
        }
      }
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

  const ensureAudioGraph = () => {
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return null;
    if (!audioCtxRef.current) {
      const ctx = new Ctx();
      const gain = ctx.createGain();
      gain.gain.value = volumeRef.current;
      gain.connect(ctx.destination);
      audioCtxRef.current = ctx;
      gainRef.current = gain;
    }
    if (gainRef.current) gainRef.current.gain.value = volumeRef.current;
    return audioCtxRef.current;
  };

  const pullChunk = () => {
    if (discardRef.current) return;
    if (modeRef.current === "mse" && !sourceBufferRef.current) return;
    const ws = mediaWsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    if (!streamActiveRef.current || awaitingRef.current || eofRef.current) return;
    awaitingRef.current = true;
    try {
      ws.send(JSON.stringify({ cmd: "music_next" }));
    } catch {
      awaitingRef.current = false;
    }
  };

  const stopStream = () => {
    suppressAudioErrorRef.current = true;
    setTimeout(() => {
      suppressAudioErrorRef.current = false;
    }, 300);
    streamActiveRef.current = false;
    awaitingRef.current = false;
    eofRef.current = false;
    discardRef.current = false;
    pendingChunkLenRef.current = 0;
    modeRef.current = "";
    timeOriginRef.current = 0;
    reopenMseRef.current = null;
    if (pcmRef.current) {
      pcmRef.current.stop();
      pcmRef.current = null;
    }
    const ws = mediaWsRef.current;
    mediaWsRef.current = null;
    if (ws) {
      const sendStop = () => {
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
      };
      if (ws.readyState === WebSocket.CONNECTING) ws.onopen = sendStop;
      else sendStop();
    }
    sourceBufferRef.current = null;
    mediaSourceRef.current = null;
    if (objectUrlRef.current) {
      try {
        URL.revokeObjectURL(objectUrlRef.current);
      } catch {
        // ignore
      }
      objectUrlRef.current = "";
    }
    const audio = audioRef.current;
    if (audio) {
      stopInProgressRef.current = true;
      try {
        audio.pause();
      } catch {
        // ignore
      }
      try {
        audio.removeAttribute("src");
        audio.load();
      } catch {
        // ignore
      }
      stopInProgressRef.current = false;
    }
  };

  // Stream over /ws/media. The length comes back with music_start, and
  // playback begins as soon as the first chunks arrive.
  const handleSelectTrack = (file, index) => {
    const audio = audioRef.current;
    if (!audio || !file) return;

    const mySeq = (streamSeqRef.current += 1);
    userPausedRef.current = false;
    stopStream();
    knownDurationRef.current = 0;
    fileSizeRef.current = 0;
    sawAudioRef.current = false;
    setDuration(0);
    setTrackMeta(null);
    setCurrentTrack(file);
    setCurrentTrackIndex(index);
    setCurrentTime(0);
    setIsPlaying(true);
    setIsPaused(false);

    const musicDir = config?.musicDir || "/music";
    const filePath = `${musicDir}/${file.name}`;
    const mime = mimeForName(file.name);
    const useMse = canUseMse(mime);
    modeRef.current = useMse ? "mse" : "pcm";

    const ctx = ensureAudioGraph();
    if (ctx) unlockContext(ctx, gainRef.current);
    if (!useMse) primeAudioElement(audio);
    if (!useMse && ctx && gainRef.current) {
      pcmRef.current = createPcmPlayer({
        ctx,
        gain: gainRef.current,
        onTime: (t) => {
          if (!isSeekingRef.current) setCurrentTime(t);
        },
        onStart: () => {
          setIsPlaying(true);
          setIsPaused(false);
        },
        onEnded: () => {
          if (mySeq !== streamSeqRef.current) return;
          handleSkipNext();
        },
        onFallback: (bytes, type) => {
          if (mySeq !== streamSeqRef.current) return;
          suppressAudioErrorRef.current = true;
          setTimeout(() => {
            suppressAudioErrorRef.current = false;
          }, 400);
          modeRef.current = "file";
          timeOriginRef.current = 0;
          const url = URL.createObjectURL(
            new Blob([bytes], { type: type || "audio/mpeg" })
          );
          if (objectUrlRef.current) {
            try {
              URL.revokeObjectURL(objectUrlRef.current);
            } catch {
              // ignore
            }
          }
          objectUrlRef.current = url;
          audio.src = url;
          const pending = audio.play();
          if (pending && typeof pending.catch === "function") {
            pending.catch((error) => {
              console.error("🎵 Phone playback failed:", error);
              setIsPlaying(false);
              setIsPaused(false);
            });
          }
        },
      });
    }

    const mediaWsUrl = buildWebSocketUrl(
      "/ws/media",
      globalConfig?.hostname || globalConfig?.deviceName || null
    );
    if (!mediaWsUrl) {
      console.error("🎵 Failed to build media WebSocket URL.");
      return;
    }

    streamActiveRef.current = true;
    eofRef.current = false;
    awaitingRef.current = false;
    let startReady = false;
    const armPull = () => {
      if (!startReady) return;
      if (useMse && !sourceBufferRef.current) return;
      pullChunk();
    };
    const ws = new WebSocket(mediaWsUrl);
    ws.binaryType = "arraybuffer";
    mediaWsRef.current = ws;

    const attachMse = () => {
      const MS = window.MediaSource || window.ManagedMediaSource;
      if (!MS) return;
      suppressAudioErrorRef.current = true;
      setTimeout(() => {
        suppressAudioErrorRef.current = false;
      }, 400);
      if (objectUrlRef.current) {
        try {
          URL.revokeObjectURL(objectUrlRef.current);
        } catch {
          // ignore
        }
        objectUrlRef.current = "";
      }
      sourceBufferRef.current = null;
      const ms = new MS();
      mediaSourceRef.current = ms;
      const objUrl = URL.createObjectURL(ms);
      objectUrlRef.current = objUrl;
      audio.src = objUrl;
      if (!userPausedRef.current) {
        const playPromise = audio.play();
        if (playPromise && typeof playPromise.catch === "function") {
          playPromise.catch(() => {});
        }
      }
      ms.addEventListener(
        "sourceopen",
        () => {
          if (mySeq !== streamSeqRef.current || sourceBufferRef.current) return;
          if (mediaSourceRef.current !== ms) return;
          try {
            const sb = ms.addSourceBuffer(mime);
            try {
              sb.mode = "sequence";
            } catch {
              // ignore
            }
            sourceBufferRef.current = sb;
            sb.addEventListener("updateend", () => {
              if (mySeq !== streamSeqRef.current) return;
              if (discardRef.current) return;
              if (streamActiveRef.current && !eofRef.current) pullChunk();
            });
            if (!discardRef.current) armPull();
          } catch (error) {
            console.error("🎵 addSourceBuffer failed:", error);
            stopStream();
          }
        },
        { once: true }
      );
    };
    if (useMse) {
      reopenMseRef.current = attachMse;
      attachMse();
    }

    const applyTrackInfo = (msg) => {
      const durationMs = Number(msg?.durationMs);
      const bitrate = Number(msg?.bitrate);
      const sampleRate = Number(msg?.sampleRate);
      const size = Number(msg?.size);
      fileSizeRef.current = Number.isFinite(size) ? size : 0;
      if (Number.isFinite(durationMs) && durationMs > 0) {
        knownDurationRef.current = durationMs / 1000;
        setDuration(durationMs / 1000);
      }
      setTrackMeta({
        durationMs: Number.isFinite(durationMs) ? durationMs : 0,
        bitrate: Number.isFinite(bitrate) ? bitrate : 0,
        sampleRate: Number.isFinite(sampleRate) ? sampleRate : 0,
        size: Number.isFinite(size) ? size : 0,
      });
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
        ws.send(
          JSON.stringify({
            cmd: "music_start",
            fs: "sd",
            path: filePath,
            mime,
            chunkSize: 16384,
          })
        );
      } catch (error) {
        console.error("🎵 Failed to send music_start:", error);
      }
    };

    ws.onmessage = async (event) => {
      if (mySeq !== streamSeqRef.current) return;
      if (typeof event.data === "string") {
        let msg;
        try {
          msg = JSON.parse(event.data);
        } catch {
          return;
        }
        if (discardRef.current && msg?.ok === false) {
          console.warn("🎵 music_seek failed:", msg?.error || "unknown");
          discardRef.current = false;
          pullChunk();
          return;
        }
        if (msg?.type === "music_start") {
          if (msg?.ok === false) {
            console.error("🎵 music_start failed:", msg?.error || "unknown");
            stopStream();
            setIsPlaying(false);
            setIsPaused(false);
            return;
          }
          applyTrackInfo(msg);
          startReady = true;
          armPull();
          return;
        }
        if (msg?.type === "music_seek") {
          discardRef.current = false;
          awaitingRef.current = false;
          eofRef.current = false;
          streamActiveRef.current = true;
          if (modeRef.current === "mse" && !sourceBufferRef.current) return;
          pullChunk();
          return;
        }
        if (msg?.type === "music_chunk") {
          if (msg?.eof) {
            awaitingRef.current = false;
            eofRef.current = true;
            if (useMse && mediaSourceRef.current?.readyState === "open") {
              try {
                mediaSourceRef.current.endOfStream();
              } catch {
                // ignore
              }
            } else if (pcmRef.current) {
              pcmRef.current.markEof();
            }
            return;
          }
          pendingChunkLenRef.current = Number(msg?.len || 0);
        }
        return;
      }

      if (!(event.data instanceof ArrayBuffer)) return;
      const expected = pendingChunkLenRef.current;
      pendingChunkLenRef.current = 0;
      awaitingRef.current = false;
      if (!expected) return;
      if (discardRef.current) return;
      const buf = new Uint8Array(event.data).slice();
      if (!sawAudioRef.current) {
        sawAudioRef.current = true;
        if (!(knownDurationRef.current > 0)) {
          const guessed = estimateTrack(buf, fileSizeRef.current);
          if (guessed?.durationMs > 0) {
            knownDurationRef.current = guessed.durationMs / 1000;
            setDuration(knownDurationRef.current);
            setTrackMeta((prev) => ({
              durationMs: guessed.durationMs,
              bitrate: prev?.bitrate || guessed.bitrate,
              sampleRate: prev?.sampleRate || guessed.sampleRate,
              size: prev?.size || fileSizeRef.current,
            }));
          }
        }
      }

      if (useMse) {
        const sb = sourceBufferRef.current;
        if (!sb) return;
        const append = () => {
          try {
            if (!sb.updating) sb.appendBuffer(buf);
            else setTimeout(append, 10);
          } catch (error) {
            console.error("🎵 appendBuffer failed:", error);
          }
        };
        append();
        if (!userPausedRef.current && audio.paused) {
          try {
            await audio.play();
          } catch {
            // playback can start on the next chunk
          }
        }
        return;
      }

      if (pcmRef.current) pcmRef.current.push(buf, mime);
      pullChunk();
    };

    ws.onerror = () => {
      if (mySeq !== streamSeqRef.current || stopInProgressRef.current) return;
      console.warn("🎵 media socket error");
    };

    ws.onclose = () => {
      if (mySeq !== streamSeqRef.current || stopInProgressRef.current) return;
      if (streamActiveRef.current && !eofRef.current) {
        stopStream();
        setIsPlaying(false);
        setIsPaused(false);
      }
    };
  };

  const handlePlayPause = async () => {
    if (!currentTrack && musicFiles.length > 0) {
      handleSelectTrack(musicFiles[0], 0);
      return;
    }
    if (!currentTrack) return;

    const ctx = ensureAudioGraph();
    if (ctx) unlockContext(ctx, gainRef.current);

    if (modeRef.current === "pcm" && pcmRef.current) {
      if (isPlaying && !isPaused) {
        userPausedRef.current = true;
        pcmRef.current.pause();
        setIsPaused(true);
        return;
      }
      userPausedRef.current = false;
      pcmRef.current.resume();
      setIsPaused(false);
      setIsPlaying(true);
      return;
    }

    const audio = audioRef.current;
    if (!audio) return;
    if (isPlaying && !isPaused) {
      userPausedRef.current = true;
      audio.pause();
      return;
    }
    if (!streamActiveRef.current && !audio.src) {
      handleSelectTrack(currentTrack, currentTrackIndex);
      return;
    }
    userPausedRef.current = false;
    try {
      await audio.play();
    } catch (error) {
      console.error("🎵 Error playing audio:", error);
    }
  };

  const handleStop = async () => {
    stopInProgressRef.current = true;
    suppressAudioErrorRef.current = true;
    setTimeout(() => {
      suppressAudioErrorRef.current = false;
    }, 250);
    stopStream();
    setCurrentTime(0);
    setIsPlaying(false);
    setIsPaused(false);
    stopInProgressRef.current = false;
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
    volumeRef.current = newVolume;
    if (audioRef.current) audioRef.current.volume = newVolume;
    if (gainRef.current) gainRef.current.gain.value = newVolume;
    if (pcmRef.current) pcmRef.current.setVolume(newVolume);

    if (isCasting) {
      console.log(
        "🎵 Note: Volume control for cast devices must be adjusted on the device itself"
      );
    }
  };

  const applySeek = (t) => {
    const limit = knownDurationRef.current || duration || t;
    const tt = Math.max(0, Math.min(Number(t) || 0, limit));
    if (modeRef.current === "file") {
      const fileAudio = audioRef.current;
      if (!fileAudio) return false;
      try {
        fileAudio.currentTime = tt;
      } catch {
        return false;
      }
      setCurrentTime(tt);
      return true;
    }
    const ws = mediaWsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return false;
    discardRef.current = true;
    awaitingRef.current = false;
    eofRef.current = false;
    streamActiveRef.current = true;
    timeOriginRef.current = tt;
    setCurrentTime(tt);
    if (modeRef.current === "pcm" && pcmRef.current) {
      pcmRef.current.seek(tt);
    } else if (modeRef.current === "mse" && reopenMseRef.current) {
      reopenMseRef.current();
    }
    try {
      ws.send(JSON.stringify({ cmd: "music_seek", ms: Math.round(tt * 1000) }));
    } catch {
      discardRef.current = false;
      return false;
    }
    return true;
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

  const trackMetaLabel = (() => {
    if (!currentTrack) return "";
    const parts = [];
    if (duration > 0) parts.push(formatTime(duration));
    if (trackMeta?.bitrate > 0) {
      parts.push(`${Math.round(trackMeta.bitrate / 1000)} kbps`);
    }
    if (trackMeta?.sampleRate > 0) {
      const khz = trackMeta.sampleRate / 1000;
      parts.push(`${Number.isInteger(khz) ? khz.toFixed(0) : khz.toFixed(1)} kHz`);
    }
    return parts.join(" · ");
  })();

  // Format file size
  const formatFileSize = (bytes) => {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    return (bytes / (1024 * 1024)).toFixed(1) + " MB";
  };

  return (
    <>
      <Module
        onResize={({ width, height }) =>
          onUpdate?.(moduleKey, { size: "custom", width, height })
        }
        onToggleWide={() => onUpdate?.(moduleKey, { wide: !config?.wide })}
        moduleConfig={config}
        online={deviceOnline}
        playback={
          isPlaying ? (isPaused ? "paused" : "playing") : undefined
        }
        onDelete={handleDeleteModule}
        onSettings={handleOpenSettings}
        settingsDisabled={!deviceOnline}
        settingsTooltip="Settings"
      >
        <InfoCard
          embedded
          fluid
          title={config?.name || SECTIONS.music.title}
          icon={SECTIONS.music.icon}
          headerActions={
            <Box
              component="span"
              sx={{
                position: "static",
                display: "inline-flex",
                alignItems: "center",
                justifyContent: "center",
                width: { xs: 34, sm: 40 },
                height: { xs: 34, sm: 40 },
              }}
            >
              {showSearch ? (
                <Box sx={{ width: { xs: 34, sm: 40 }, height: { xs: 34, sm: 40 } }} />
              ) : (
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
                    }}
                  >
                    <SearchIcon sx={{ fontSize: { xs: 18, sm: 22 } }} />
                  </IconButton>
                </span>
              </Tooltip>
              )}

              <Box
                component="span"
                sx={{
                  position: "absolute",
                  right: 0,
                  top: "50%",
                  transform: "translateY(-50%)",
                  zIndex: 2,
                  boxSizing: "border-box",
                  display: showSearch ? "block" : "none",
                  width: "min(100%, 360px)",
                  maxWidth: "100%",
                }}
              >
                <TextField
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  placeholder="Search…"
                  size="small"
                  autoComplete="off"
                  autoFocus={showSearch}
                  sx={{
                    width: "100%",
                    maxWidth: "100%",
                    minWidth: 0,
                    boxSizing: "border-box",
                    "& .MuiInputBase-root": {
                      boxSizing: "border-box",
                      height: { xs: 32, sm: 36 },
                      fontSize: { xs: "0.8rem", sm: "0.85rem" },
                      backgroundColor: "background.paper",
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
          }
        >
        <Box
          sx={{
            width: "100%",
            maxWidth: "100%",
            boxSizing: "border-box",
            flex: "1 1 auto",
            minHeight: 0,
            display: "flex",
            flexDirection: "column",
            gap: { xs: 0.5, sm: 1 },
            overflow: "hidden",
          }}
        >
          {/* Current track display */}
          <Box
            sx={{
              width: "100%",
              maxWidth: "100%",
              boxSizing: "border-box",
              flex: "0 0 auto",
              display: "flex",
              flexDirection: "column",
              alignItems: "stretch",
              justifyContent: "flex-start",
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
                fontSize: { xs: "0.8rem", sm: "0.9rem" },
                lineHeight: 1.35,
                width: "100%",
                flex: "0 0 auto",
                whiteSpace: "normal",
                overflowWrap: "anywhere",
                textAlign: "center",
              }}
            >
              {currentTrack ? currentTrack.name : "No track selected"}
            </Typography>
            {trackMetaLabel ? (
              <Typography
                variant="caption"
                color="text.secondary"
                align="center"
                sx={{
                  fontSize: { xs: "0.7rem", sm: "0.75rem" },
                  lineHeight: 1.3,
                  width: "100%",
                  flex: "0 0 auto",
                  mt: 0.25,
                  textAlign: "center",
                  fontVariantNumeric: "tabular-nums",
                }}
              >
                {trackMetaLabel}
              </Typography>
            ) : null}

            <Box
              sx={{
                width: "100%",
                maxWidth: "100%",
                boxSizing: "border-box",
                mt: 0.25,
                display: "flex",
                flexDirection: "column",
                alignItems: "stretch",
                gap: 0.5,
              }}
            >
              <Box
                sx={{
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

              <Box
                sx={{
                  display: "flex",
                  alignItems: "center",
                  gap: 0.75,
                  width: "100%",
                  minWidth: 0,
                }}
              >
              <Typography
                variant="caption"
                color="text.secondary"
                sx={{
                  fontSize: { xs: "0.55rem", sm: "0.7rem" },
                  flex: "0 0 auto",
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
                  flex: "1 1 auto",
                  width: "auto",
                  minWidth: 0,
                  mx: 0.25,
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
                  fontSize: { xs: "0.55rem", sm: "0.7rem" },
                  flex: "0 0 auto",
                  textAlign: "right",
                  opacity: duration > 0 ? 1 : 0.6,
                  fontVariantNumeric: "tabular-nums",
                }}
              >
                {formatTime(duration)}
              </Typography>

              <Box
                sx={{
                  display: "flex",
                  alignItems: "center",
                  flex: "0 1 auto",
                  gap: 0.5,
                  minWidth: 0,
                  maxWidth: "46%",
                  opacity: isCasting ? 0.5 : 1,
                }}
              >
                  <VolumeUpIcon sx={{ fontSize: { xs: 18, sm: 20 }, flex: "0 0 auto" }} />
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
                      width: "auto",
                      flex: "1 1 72px",
                      minWidth: 48,
                      maxWidth: 96,
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
              </Box>
              </Box>
            </Box>
          </Box>

          {/* Track list. Marked so a finger scroll does not drag the module. */}
          <Box
            data-no-dnd="true"
            sx={{
              width: "100%",
              maxWidth: "100%",
              boxSizing: "border-box",
              overflow: "auto",
              touchAction: "pan-y",
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
              flex: "1 1 auto",
              minHeight: 0,
              ...moduleScrollbarSx,
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
                            lineHeight: 1.3,
                            whiteSpace: "normal",
                            overflowWrap: "anywhere",
                            display: "-webkit-box",
                            WebkitLineClamp: 2,
                            WebkitBoxOrient: "vertical",
                            overflow: "hidden",
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
        </InfoCard>
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
