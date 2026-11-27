import React, { useEffect, useMemo, useRef, useState } from "react";
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
import SearchIcon from "@mui/icons-material/Search";
import CloseIcon from "@mui/icons-material/Close";
import VolumeUpIcon from "@mui/icons-material/VolumeUp";
import FullscreenIcon from "@mui/icons-material/Fullscreen";
import MoviePlayerSettingsModal from "./MoviePlayerSettingsModal";
import { buildWebSocketUrl } from "../../../utils/api";
import { openMovieFile } from "./movieStream";
import { createMoviePlayback } from "./moviePlayback";

const MOVIE_EXTENSIONS = [".mp4", ".m4v", ".mov", ".mkv", ".webm"];

function formatClock(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return "0:00";
  const whole = Math.floor(seconds);
  const h = Math.floor(whole / 3600);
  const m = Math.floor((whole % 3600) / 60);
  const s = whole % 60;
  const ss = String(s).padStart(2, "0");
  if (h > 0) return `${h}:${String(m).padStart(2, "0")}:${ss}`;
  return `${m}:${ss}`;
}

function joinPath(dir, name) {
  const base = String(dir || "/movies").replace(/\/+$/, "") || "/movies";
  return `${base}/${name}`;
}

export default function MoviePlayerModule({
  config,
  globalConfig,
  onUpdate,
  onDelete,
  deviceOnline = true,
  controlWs,
}) {
  const moduleKey = config?.key;
  const [movies, setMovies] = useState([]);
  const [current, setCurrent] = useState(null);
  const [currentIndex, setCurrentIndex] = useState(-1);
  const [isPlaying, setIsPlaying] = useState(false);
  const [loading, setLoading] = useState(false);
  const [opening, setOpening] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [searchQuery, setSearchQuery] = useState("");
  const [showSearch, setShowSearch] = useState(false);
  const [settingsData, setSettingsData] = useState({
    movieDir: config?.movieDir || "/movies",
  });
  const [duration, setDuration] = useState(0);
  const [currentTime, setCurrentTime] = useState(0);
  const [volume, setVolume] = useState(0.7);
  const [error, setError] = useState("");
  const [warning, setWarning] = useState("");

  const canvasRef = useRef(null);
  const stageRef = useRef(null);
  const playerRef = useRef(null);
  const seqRef = useRef(0);
  const shownSecondRef = useRef(-1);
  const durationRef = useRef(0);
  const seekingRef = useRef(false);

  const filtered = useMemo(() => {
    const q = (searchQuery || "").trim().toLowerCase();
    if (!q) return movies;
    return movies.filter((file) =>
      String(file?.name || "").toLowerCase().includes(q)
    );
  }, [movies, searchQuery]);

  useEffect(() => {
    setSettingsData({ movieDir: config?.movieDir || "/movies" });
  }, [config?.movieDir]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return undefined;
    const player = createMoviePlayback(canvas, {
      onTime: (time, total) => {
        if (seekingRef.current) return;
        const second = Math.floor(time);
        if (second !== shownSecondRef.current) {
          shownSecondRef.current = second;
          setCurrentTime(time);
        }
        if (total > 0 && total !== durationRef.current) {
          durationRef.current = total;
          setDuration(total);
        }
      },
      onEnded: () => setIsPlaying(false),
      onReady: ({ duration: total, warning: note }) => {
        durationRef.current = total || 0;
        setDuration(total || 0);
        setWarning(note || "");
        setCurrentTime(0);
        shownSecondRef.current = 0;
      },
      onError: (err) => {
        setError(err?.message || "Playback failed");
        setIsPlaying(false);
      },
    });
    playerRef.current = player;
    player.setVolume(volume);
    return () => {
      player.stop();
      playerRef.current = null;
    };
  }, []);

  useEffect(() => {
    if (!deviceOnline) {
      seqRef.current += 1;
      playerRef.current?.stop();
      setIsPlaying(false);
      setOpening(false);
    }
  }, [deviceOnline]);

  const sendWsCommand = async (cmd) => {
    if (!controlWs || controlWs.readyState !== WebSocket.OPEN) {
      throw new Error("WebSocket not connected");
    }
    return await controlWs.sendCommand(cmd, 10000);
  };

  const loadMovies = async () => {
    if (!deviceOnline || !controlWs || controlWs.readyState !== WebSocket.OPEN) {
      return;
    }
    setLoading(true);
    try {
      const movieDir = config?.movieDir || "/movies";
      const response = await sendWsCommand({
        cmd: "list_files",
        fs: "sd",
        path: movieDir,
      });
      const files = (response.files || []).filter(
        (file) =>
          !file.isDirectory &&
          MOVIE_EXTENSIONS.some((ext) => file.name.toLowerCase().endsWith(ext))
      );
      setMovies(files);
    } catch (err) {
      console.warn("🎬 Error loading movies:", err.message || err);
      setMovies([]);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    if (deviceOnline && controlWs && controlWs.readyState === WebSocket.OPEN) {
      const timer = setTimeout(loadMovies, 500);
      return () => clearTimeout(timer);
    }
  }, [deviceOnline, config?.movieDir, controlWs]);

  const playMovie = async (file, index) => {
    const player = playerRef.current;
    if (!player || !file) return;
    const seq = ++seqRef.current;
    setCurrent(file);
    setCurrentIndex(index);
    setError("");
    setWarning("");
    setOpening(true);
    setIsPlaying(false);
    setCurrentTime(0);
    setDuration(0);
    durationRef.current = 0;
    shownSecondRef.current = -1;
    try {
      const mediaWsUrl = buildWebSocketUrl(
        "/ws/media",
        globalConfig?.hostname || globalConfig?.deviceName || null
      );
      if (!mediaWsUrl) throw new Error("Media socket is unavailable.");
      const handle = openMovieFile(
        mediaWsUrl,
        joinPath(config?.movieDir || "/movies", file.name)
      );
      await player.load(handle);
      if (seq !== seqRef.current) return;
      await player.play();
      if (seq !== seqRef.current) return;
      setIsPlaying(true);
    } catch (err) {
      if (seq !== seqRef.current) return;
      console.error("🎬 Playback failed:", err);
      setError(err?.message || "Playback failed");
      setIsPlaying(false);
    } finally {
      if (seq === seqRef.current) setOpening(false);
    }
  };

  const handlePlayPause = () => {
    const player = playerRef.current;
    if (!player || opening) return;
    if (!current && filtered.length > 0) {
      const index = movies.findIndex((file) => file.name === filtered[0].name);
      playMovie(filtered[0], index);
      return;
    }
    if (!current) return;
    if (player.isPlaying()) {
      player.pause();
      setIsPlaying(false);
    } else {
      player.play().then(() => setIsPlaying(true));
    }
  };

  const handleStop = () => {
    seqRef.current += 1;
    playerRef.current?.stop();
    setIsPlaying(false);
    setOpening(false);
    setError("");
    setCurrentTime(0);
    shownSecondRef.current = 0;
  };

  const stepMovie = (delta) => {
    if (!movies.length) return;
    const next = currentIndex + delta;
    if (next < 0 || next >= movies.length) return;
    playMovie(movies[next], next);
  };

  const handleSeek = (value) => {
    seekingRef.current = false;
    const player = playerRef.current;
    if (!player || !current) return;
    player.seek(value).then(() => {
      setCurrentTime(value);
      shownSecondRef.current = Math.floor(value);
    });
  };

  const handleFullscreen = () => {
    const node = stageRef.current;
    if (!node) return;
    if (document.fullscreenElement) {
      document.exitFullscreen?.();
      return;
    }
    node.requestFullscreen?.();
  };

  const handleSaveSettings = (updated) => {
    if (onUpdate && moduleKey && updated) {
      onUpdate(moduleKey, { movieDir: updated.movieDir });
    }
    setSettingsOpen(false);
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
        playback={current ? (isPlaying ? "playing" : "paused") : undefined}
        onDelete={() => onDelete?.(moduleKey)}
        onSettings={() => setSettingsOpen(true)}
        settingsDisabled={!deviceOnline}
        settingsTooltip="Settings"
      >
        <InfoCard
          embedded
          fluid
          title={config?.name || SECTIONS.movie.title}
          icon={SECTIONS.movie.icon}
          headerActions={
            <Box component="span" sx={{ display: "inline-flex" }}>
              {showSearch ? (
                <Box sx={{ width: 34, height: 34 }} />
              ) : (
                <Tooltip title="Search movies">
                  <span>
                    <IconButton
                      onClick={() => setShowSearch(true)}
                      size="small"
                      sx={{ color: searchQuery ? "secondary.main" : "primary.main" }}
                    >
                      <SearchIcon />
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
                  display: showSearch ? "block" : "none",
                  width: "min(100%, 360px)",
                }}
              >
                <TextField
                  value={searchQuery}
                  onChange={(e) => setSearchQuery(e.target.value)}
                  placeholder="Search…"
                  size="small"
                  autoFocus={showSearch}
                  sx={{
                    width: "100%",
                    "& .MuiInputBase-root": {
                      height: 36,
                      backgroundColor: "background.paper",
                    },
                  }}
                  InputProps={{
                    endAdornment: (
                      <InputAdornment position="end">
                        <IconButton size="small" onClick={() => setShowSearch(false)}>
                          <CloseIcon fontSize="small" />
                        </IconButton>
                      </InputAdornment>
                    ),
                  }}
                />
              </Box>
            </Box>
          }
        >
          <Box
            ref={stageRef}
            data-no-dnd="true"
            onClick={handlePlayPause}
            sx={{
              position: "relative",
              width: "100%",
              aspectRatio: "16 / 9",
              flex: "0 0 auto",
              bgcolor: "black",
              borderRadius: 1,
              overflow: "hidden",
              cursor: "pointer",
            }}
          >
            <canvas
              ref={canvasRef}
              style={{ width: "100%", height: "100%", objectFit: "contain", display: "block" }}
            />
            {(opening || error || (!current && !opening)) && (
              <Box
                sx={{
                  position: "absolute",
                  inset: 0,
                  display: "flex",
                  alignItems: "center",
                  justifyContent: "center",
                  px: 2,
                  textAlign: "center",
                  pointerEvents: "none",
                }}
              >
                {opening ? (
                  <CircularProgress size={28} />
                ) : (
                  <Typography variant="body2" sx={{ color: error ? "error.main" : "grey.400" }}>
                    {error || "Choose a movie"}
                  </Typography>
                )}
              </Box>
            )}
          </Box>

          {warning ? (
            <Typography variant="caption" color="warning.main" sx={{ mt: 0.5 }}>
              {warning}
            </Typography>
          ) : null}

          <Box data-no-dnd="true" sx={{ display: "flex", flexDirection: "column", gap: 0.5, mt: 1 }}>
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
              <Typography variant="caption" sx={{ minWidth: 36 }}>
                {formatClock(currentTime)}
              </Typography>
              <Slider
                size="small"
                min={0}
                max={duration > 0 ? duration : 0}
                step={0.1}
                value={Math.min(currentTime, duration || 0)}
                disabled={!current || duration <= 0}
                onChange={(_, value) => {
                  seekingRef.current = true;
                  setCurrentTime(value);
                }}
                onChangeCommitted={(_, value) => handleSeek(value)}
                sx={{ flex: 1, py: 0.5 }}
              />
              <Typography variant="caption" sx={{ minWidth: 36, textAlign: "right" }}>
                {formatClock(duration)}
              </Typography>
            </Box>
            <Box sx={{ display: "flex", alignItems: "center", justifyContent: "space-between" }}>
              <Box sx={{ display: "flex", alignItems: "center" }}>
                <Tooltip title="Previous">
                  <span>
                    <IconButton
                      size="small"
                      onClick={() => stepMovie(-1)}
                      disabled={currentIndex <= 0}
                      sx={{ color: "primary.main" }}
                    >
                      <SkipPreviousIcon />
                    </IconButton>
                  </span>
                </Tooltip>
                <Tooltip title={isPlaying ? "Pause" : "Play"}>
                  <span>
                    <IconButton size="small" onClick={handlePlayPause} sx={{ color: "primary.main" }}>
                      {isPlaying ? <PauseIcon /> : <PlayArrowIcon />}
                    </IconButton>
                  </span>
                </Tooltip>
                <Tooltip title="Stop">
                  <span>
                    <IconButton
                      size="small"
                      onClick={handleStop}
                      disabled={!current}
                      sx={{ color: "primary.main" }}
                    >
                      <StopIcon />
                    </IconButton>
                  </span>
                </Tooltip>
                <Tooltip title="Next">
                  <span>
                    <IconButton
                      size="small"
                      onClick={() => stepMovie(1)}
                      disabled={currentIndex < 0 || currentIndex >= movies.length - 1}
                      sx={{ color: "primary.main" }}
                    >
                      <SkipNextIcon />
                    </IconButton>
                  </span>
                </Tooltip>
              </Box>
              <Box sx={{ display: "flex", alignItems: "center", minWidth: 0, flex: "1 1 120px", maxWidth: 180 }}>
                <VolumeUpIcon sx={{ fontSize: 18, color: "primary.main", mr: 0.5 }} />
                <Slider
                  size="small"
                  min={0}
                  max={1}
                  step={0.01}
                  value={volume}
                  onChange={(_, value) => {
                    setVolume(value);
                    playerRef.current?.setVolume(value);
                  }}
                  sx={{ py: 0.5 }}
                />
                <Tooltip title="Fullscreen">
                  <IconButton size="small" onClick={handleFullscreen} sx={{ color: "primary.main" }}>
                    <FullscreenIcon />
                  </IconButton>
                </Tooltip>
              </Box>
            </Box>
          </Box>

          <Box
            data-no-dnd="true"
            sx={{
              width: "100%",
              overflow: "auto",
              touchAction: "pan-y",
              backgroundColor: "rgba(0, 0, 0, 0.1)",
              borderRadius: 1,
              flex: "1 1 auto",
              minHeight: 0,
              mt: 1,
              ...moduleScrollbarSx,
            }}
          >
            {loading ? (
              <Box sx={{ display: "flex", justifyContent: "center", py: 2 }}>
                <CircularProgress size={20} />
              </Box>
            ) : filtered.length === 0 ? (
              <Box sx={{ p: 2, textAlign: "center" }}>
                <Typography variant="body2" color="text.secondary">
                  {movies.length === 0
                    ? `No movies found in ${config?.movieDir || "/movies"}`
                    : "No matches"}
                </Typography>
              </Box>
            ) : (
              <List dense sx={{ py: 0.5, width: "100%" }}>
                {filtered.map((file) => {
                  const index = movies.findIndex((item) => item.name === file.name);
                  const selected = current?.name === file.name;
                  return (
                    <ListItem key={file.name} disablePadding>
                      <ListItemButton
                        selected={selected}
                        onClick={() => playMovie(file, index)}
                      >
                        <ListItemText
                          primary={file.name}
                          primaryTypographyProps={{
                            sx: {
                              fontSize: "0.85rem",
                              overflowWrap: "anywhere",
                              display: "-webkit-box",
                              WebkitLineClamp: 2,
                              WebkitBoxOrient: "vertical",
                              overflow: "hidden",
                            },
                          }}
                        />
                      </ListItemButton>
                    </ListItem>
                  );
                })}
              </List>
            )}
          </Box>
        </InfoCard>
      </Module>
      <MoviePlayerSettingsModal
        open={settingsOpen}
        onClose={() => setSettingsOpen(false)}
        onSave={handleSaveSettings}
        movieData={settingsData}
        onMovieDataChange={setSettingsData}
      />
    </>
  );
}
