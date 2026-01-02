import React, { useState, useEffect, useRef, useCallback } from "react";
import {
  Box,
  Alert,
  Typography,
  Card,
  CardContent,
  Paper,
  useTheme,
  useMediaQuery,
  IconButton,
  Tooltip,
  Menu,
  MenuItem,
} from "@mui/material";
import DescriptionIcon from "@mui/icons-material/Description";
import PowerSettingsNewIcon from "@mui/icons-material/PowerSettingsNew";
import PowerOffIcon from "@mui/icons-material/PowerOff";
import AutorenewIcon from "@mui/icons-material/Autorenew";
import VerticalAlignBottomIcon from "@mui/icons-material/VerticalAlignBottom";
import VerticalAlignBottomOutlinedIcon from "@mui/icons-material/VerticalAlignBottomOutlined";
import VerticalAlignTopIcon from "@mui/icons-material/VerticalAlignTop";
import FilterListIcon from "@mui/icons-material/FilterList";
import WrapTextIcon from "@mui/icons-material/WrapText";
import NotesIcon from "@mui/icons-material/Notes";
import SettingsModal from "./SettingsModal";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";

export default function LogsSettingsModal({
  config,
  saveConfigToDevice,
  open = false,
  onClose,
}) {
  const theme = useTheme();
  const isMobile = useMediaQuery(theme.breakpoints.down("sm"));

  const [logs, setLogs] = useState("");
  const [logLoading, setLogLoading] = useState(false);
  const [logError, setLogError] = useState("");
  const [logEnabled, setLogEnabled] = useState(config?.log?.enabled !== false);
  const [logLevel, setLogLevel] = useState(config?.log?.level || "info");
  // Auto refresh interval: null (disabled) or 3 seconds
  const [refreshInterval, setRefreshInterval] = useState(null);
  const [autoScroll, setAutoScroll] = useState(false);
  const [lineWrap, setLineWrap] = useState(false);
  const [logLevelMenuAnchor, setLogLevelMenuAnchor] = useState(null);
  const logContainerRef = useRef(null);
  const refreshIntervalRef = useRef(null);
  const scrollSentinelRef = useRef(null);
  const observerRef = useRef(null);
  const hasScrolledInitiallyRef = useRef(false);
  const prevAutoScrollRef = useRef(autoScroll);
  const [isAtBottom, setIsAtBottom] = useState(true);

  const menuRowSx = (extra = {}) => ({
    display: "grid",
    gridTemplateColumns: isMobile ? "1fr" : "44px minmax(0, 1fr)",
    alignItems: "center",
    justifyItems: isMobile ? "center" : "start",
    columnGap: 0.75,
    cursor: "pointer",
    width: "100%",
    minWidth: 0,
    overflow: "hidden",
    px: isMobile ? 0 : 0.5,
    py: isMobile ? 0 : 0.25,
    borderRadius: 1,
    "&:hover": isMobile
      ? { opacity: 0.85 }
      : { opacity: 0.9, backgroundColor: "action.hover" },
    ...extra,
  });

  const menuIconSx = (color) => ({
    color,
    pointerEvents: "none",
    p: isMobile ? 0.25 : 0.5,
    justifySelf: isMobile ? "center" : "center",
    "& svg": {
      fontSize: isMobile ? 20 : 24,
    },
  });

  const menuLabelSx = (color, extra = {}) => ({
    color,
    pointerEvents: "none",
    fontSize: "0.9rem",
    minWidth: 0,
    display: "block",
    whiteSpace: "nowrap",
    overflow: "hidden",
    textOverflow: "ellipsis",
    ...extra,
  });

  const scrollToTop = useCallback(() => {
    const container = logContainerRef.current;
    if (!container) return;
    container.scrollTop = 0;
    // If user intentionally scrolls away, disable auto-scroll so UI matches behavior
    setAutoScroll(false);
    // Ensure we don't immediately auto-scroll back down on next update
    setIsAtBottom(false);
    hasScrolledInitiallyRef.current = true;
  }, []);

  // If user manually scrolls away from the bottom, disable auto-scroll so the UI reflects it.
  const handleLogScroll = useCallback(() => {
    if (!autoScroll) return;
    const container = logContainerRef.current;
    if (!container) return;

    const distanceFromBottom =
      container.scrollHeight - container.scrollTop - container.clientHeight;

    // If user is not near the bottom, treat this as a manual scroll-up and disable auto-scroll.
    if (distanceFromBottom > 100) {
      setAutoScroll(false);
    }
  }, [autoScroll]);

  // Use Intersection Observer API (industry standard) to detect if user is at bottom
  useEffect(() => {
    if (!autoScroll) {
      setIsAtBottom(true); // Default to true when auto-scroll is disabled
      if (observerRef.current) {
        observerRef.current.disconnect();
        observerRef.current = null;
      }
      return;
    }

    // Wait for next tick to ensure DOM is ready
    const timeoutId = setTimeout(() => {
      const container = logContainerRef.current;
      const sentinel = scrollSentinelRef.current;

      if (!container || !sentinel) {
        return;
      }

      // Clean up existing observer
      if (observerRef.current) {
        observerRef.current.disconnect();
      }

      // Create Intersection Observer to watch the sentinel element
      const observer = new IntersectionObserver(
        (entries) => {
          // If sentinel is visible, user is at (or near) bottom
          setIsAtBottom(entries[0].isIntersecting);
        },
        {
          root: container, // Observe within the scroll container
          rootMargin: "0px 0px 100px 0px", // Consider "at bottom" if within 100px
          threshold: 0,
        }
      );

      observer.observe(sentinel);
      observerRef.current = observer;

      // Initial check - if sentinel is already visible, we're at bottom
      const rect = sentinel.getBoundingClientRect();
      const containerRect = container.getBoundingClientRect();
      if (
        rect.top >= containerRect.top &&
        rect.bottom <= containerRect.bottom + 100
      ) {
        setIsAtBottom(true);
      }
    }, 150);

    return () => {
      clearTimeout(timeoutId);
      if (observerRef.current) {
        observerRef.current.disconnect();
        observerRef.current = null;
      }
    };
  }, [autoScroll, logs]); // Re-run when logs change to ensure observer is set up after DOM update

  // Fetch logs from device
  const fetchLogs = async () => {
    setLogLoading(true);
    setLogError("");

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 10000);

    try {
      // The device serves logs as a normal file:
      // - SD:  /sd/<logFilePath>
      // - LFS: /lfs/<logFilePath>
      //
      // The log file path can be configured in firmware via config.log.file or
      // config.log.filePath; default is "/espwifi.log".
      let logFilePath =
        config?.log?.file || config?.log?.filePath || "/espwifi.log";
      if (typeof logFilePath !== "string" || logFilePath.trim() === "") {
        logFilePath = "/espwifi.log";
      }
      if (!logFilePath.startsWith("/")) {
        logFilePath = `/${logFilePath}`;
      }

      // Avoid fetching huge log files (can exceed UI timeout and cause the device
      // to see ECONNRESET mid-stream). Ask for the last ~64KB.
      const tailBytes = 256 * 1024;

      // Determine filesystem preference from config
      const useSD = config?.log?.useSD !== false; // Default to SD if not specified

      // Build candidates list based on filesystem preference
      const candidates = useSD
        ? [
            `/sd${logFilePath}?tail=${tailBytes}`,
            `/lfs${logFilePath}?tail=${tailBytes}`, // Fallback to LFS
          ]
        : [
            `/lfs${logFilePath}?tail=${tailBytes}`,
            `/sd${logFilePath}?tail=${tailBytes}`, // Fallback to SD
          ];
      let lastResponse = null;

      for (const path of candidates) {
        const fetchUrl = buildApiUrl(path);
        // eslint-disable-next-line no-await-in-loop
        const response = await fetch(
          fetchUrl,
          getFetchOptions({
            signal: controller.signal,
          })
        );
        lastResponse = response;

        if (response.ok) {
          // eslint-disable-next-line no-await-in-loop
          const text = await response.text();
          setLogs(text);
          setLogError("");
          clearTimeout(timeoutId);
          return;
        }

        // If SD isn't mounted or file doesn't exist there, fall back to LFS.
        if (response.status === 404 || response.status === 503) {
          continue;
        }

        // Auth errors etc should be surfaced immediately.
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      clearTimeout(timeoutId);

      // Both candidates failed.
      if (lastResponse?.status === 404) {
        setLogs(
          "Log file not found. Logging may be disabled or the file hasn't been created yet."
        );
        setLogError("");
      } else if (lastResponse?.status === 503) {
        setLogError("Filesystem not available");
      } else {
        throw new Error(
          `Failed to fetch logs${
            lastResponse ? ` (HTTP ${lastResponse.status})` : ""
          }`
        );
      }
    } catch (error) {
      clearTimeout(timeoutId);
      if (error.name === "AbortError") {
        setLogError("Request timed out - device may be offline");
      } else {
        console.error("Failed to fetch logs:", error);
        setLogError(`Failed to fetch logs: ${error.message}`);
      }
    } finally {
      setLogLoading(false);
    }
  };

  // Scroll to bottom of log container
  const scrollToBottom = useCallback(() => {
    const container = logContainerRef.current;
    if (!container) return;

    // Use double requestAnimationFrame for reliable DOM timing
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        if (container) {
          container.scrollTop = container.scrollHeight;
          // Verify we're at bottom after scrolling
          const isReallyAtBottom =
            Math.abs(
              container.scrollHeight -
                container.scrollTop -
                container.clientHeight
            ) < 10;
          if (isReallyAtBottom) {
            setIsAtBottom(true);
          }
        }
      });
    });
  }, []);

  // Auto-scroll to bottom when logs update or autoScroll is enabled (if user is at bottom)
  useEffect(() => {
    if (!autoScroll || !logContainerRef.current || !logs) {
      prevAutoScrollRef.current = autoScroll;
      return;
    }

    // Check if autoScroll was just enabled (transitioned from false to true)
    const wasJustEnabled = !prevAutoScrollRef.current && autoScroll;
    prevAutoScrollRef.current = autoScroll;

    // Only auto-scroll if user is at bottom (hasn't manually scrolled up)
    // Exception: always scroll on initial load, toggle-on, or when logs change and we're at bottom
    if (!isAtBottom && hasScrolledInitiallyRef.current && !wasJustEnabled) {
      return;
    }

    // Wait for DOM to update with new log content, then scroll
    const timeoutId = setTimeout(() => {
      scrollToBottom();
      hasScrolledInitiallyRef.current = true;
    }, 150);

    return () => {
      clearTimeout(timeoutId);
    };
  }, [logs, autoScroll, isAtBottom, scrollToBottom]);

  // Auto-refresh setup
  useEffect(() => {
    if (refreshInterval && refreshInterval > 0) {
      // Initial fetch
      fetchLogs();

      // Set up interval
      const intervalId = setInterval(() => {
        fetchLogs();
      }, refreshInterval * 1000);

      refreshIntervalRef.current = intervalId;

      return () => {
        if (refreshIntervalRef.current) {
          clearInterval(refreshIntervalRef.current);
          refreshIntervalRef.current = null;
        }
      };
    } else {
      // Clear interval if auto-refresh is disabled
      if (refreshIntervalRef.current) {
        clearInterval(refreshIntervalRef.current);
        refreshIntervalRef.current = null;
      }
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [refreshInterval]);

  // Update log settings
  const updateLogSettings = async (enabled, level) => {
    try {
      // Validate log level
      const validLevels = ["access", "debug", "info", "warning", "error"];
      if (level && !validLevels.includes(level)) {
        throw new Error(
          "Invalid log level. Must be: access, debug, info, warning, or error"
        );
      }

      // Create partial config with just the log section
      const partialConfig = {
        log: {
          enabled: enabled,
          level: level,
        },
      };

      // Use saveConfigToDevice which handles PUT /config
      await saveConfigToDevice(partialConfig);
    } catch (error) {
      console.error("Failed to update log settings:", error);
      setLogError(`Failed to update log settings: ${error.message}`);
    }
  };

  // Handle log enabled change
  const handleLogEnabledChange = () => {
    const newValue = !logEnabled;
    setLogEnabled(newValue);
    // Update immediately
    updateLogSettings(newValue, logLevel);
  };

  // Handle log level change
  const handleLogLevelChange = (newValue) => {
    setLogLevel(newValue);
    setLogLevelMenuAnchor(null);
    // Update immediately
    updateLogSettings(logEnabled, newValue);
  };

  const handleLogLevelMenuOpen = (event) => {
    setLogLevelMenuAnchor(event.currentTarget);
  };

  const handleLogLevelMenuClose = () => {
    setLogLevelMenuAnchor(null);
  };

  // Handle view logs in new window (must include auth headers; window.open can't set headers)
  const handleViewLogs = async () => {
    // Open first to avoid popup blockers, then navigate once we have the blob URL
    const newTab = window.open("", "_blank");
    if (newTab) {
      newTab.document.title = "Logs";
      newTab.document.body.style.background = "#111";
      newTab.document.body.style.color = "#fff";
      newTab.document.body.style.fontFamily = "system-ui, sans-serif";
      newTab.document.body.style.padding = "16px";
      newTab.document.body.textContent = "Loading logs…";
    }

    const fetchUrl = buildApiUrl("/logs");
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 10000);

    try {
      const response = await fetch(
        fetchUrl,
        getFetchOptions({ method: "GET", signal: controller.signal })
      );

      clearTimeout(timeoutId);

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      const contentType =
        response.headers.get("content-type") || "text/plain;charset=utf-8";
      const blob = await response.blob();
      const blobUrl = URL.createObjectURL(
        new Blob([blob], { type: contentType })
      );

      if (newTab) {
        newTab.location.href = blobUrl;
      } else {
        window.open(blobUrl, "_blank");
      }

      // Revoke after navigation has time to happen
      setTimeout(() => URL.revokeObjectURL(blobUrl), 60_000);
    } catch (error) {
      clearTimeout(timeoutId);
      const msg =
        error?.name === "AbortError"
          ? "Request timed out - device may be offline"
          : `Failed to open logs: ${error.message || error}`;

      setLogError(msg);
      if (newTab) {
        newTab.document.body.textContent = msg;
      }
    }
  };

  // Initial fetch on mount (only when modal is open)
  useEffect(() => {
    if (open) {
      fetchLogs();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  // Update local state when config changes
  useEffect(() => {
    if (config) {
      setLogEnabled(config.log?.enabled !== false);
      setLogLevel(config.log?.level || "info");
    }
  }, [config]);

  return (
    <SettingsModal
      open={open}
      onClose={onClose}
      title={
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 1,
            width: "100%",
          }}
        >
          <DescriptionIcon color="primary" />
          <span>Logs</span>
        </Box>
      }
      maxWidth={false}
      paperSx={{
        // Better desktop use of space; keep mobile behavior intact
        width: { xs: "99%", sm: "99vw" },
        minWidth: { xs: "90%", sm: "650px" },
        maxWidth: { xs: "99%", sm: "95vw" },
        // Stretch a bit further toward the bottom of the viewport on desktop
        height: { xs: "90%", sm: "88vh" },
        maxHeight: { xs: "90%", sm: "88vh" },
      }}
      // Keep the left rail fixed; only the log output should scroll
      contentSx={{
        overflowY: "hidden",
        // Tighter content spacing so more fits on screen
        p: { xs: 2, sm: 1.5 },
        pt: { xs: 1.5, sm: 1.25 },
        pb: { xs: 1.5, sm: 1.25 },
      }}
    >
      <Box
        sx={{
          display: "flex",
          flexDirection: isMobile ? "column" : "row",
          gap: 0.75,
          alignItems: "stretch",
          height: "100%",
          flex: 1,
          minHeight: 0,
        }}
      >
        {/* Controls */}
        <Card
          sx={{
            // On desktop the modal content doesn't scroll; the log output does.
            // So we can keep this as a full-height left rail without sticky positioning.
            position: isMobile ? "sticky" : "relative",
            top: isMobile ? 0 : "auto",
            zIndex: 10,
            bgcolor: "background.paper",
            boxShadow: 2,
            flex: isMobile ? "0 0 auto" : "0 0 200px",
            height: isMobile ? "auto" : "100%",
            minHeight: 0,
            maxHeight: isMobile ? undefined : "100%",
            overflow: isMobile ? undefined : "auto",
            display: "flex",
            flexDirection: "column",
            alignSelf: "stretch",
          }}
        >
          <CardContent
            sx={{
              py: isMobile ? 1.25 : 1.5,
              px: isMobile ? 1.5 : 1.5,
              flex: 1,
            }}
          >
            <Box
              sx={{
                display: isMobile ? "grid" : "flex",
                gridTemplateColumns: isMobile
                  ? "repeat(4, minmax(0, 1fr))"
                  : undefined,
                justifyItems: isMobile ? "center" : undefined,
                flexDirection: isMobile ? undefined : "column",
                flexWrap: isMobile ? undefined : "nowrap",
                alignItems: isMobile ? "center" : "stretch",
                gap: isMobile ? 1 : 1,
                // Keep mobile rows balanced; avoid a single icon on its own line.
              }}
            >
              {/* Enable/Disable Logging */}
              <Tooltip
                title={
                  logEnabled
                    ? "Click to disable Logging"
                    : "Click to enable Logging"
                }
              >
                <Box onClick={handleLogEnabledChange} sx={menuRowSx()}>
                  <IconButton
                    size="small"
                    sx={menuIconSx(
                      logEnabled ? "primary.main" : "text.disabled"
                    )}
                  >
                    {logEnabled ? <PowerSettingsNewIcon /> : <PowerOffIcon />}
                  </IconButton>
                  {!isMobile && (
                    <Typography
                      variant="body2"
                      sx={menuLabelSx(
                        logEnabled ? "primary.main" : "text.disabled"
                      )}
                    >
                      {logEnabled ? "Logging Enabled" : "Logging Disabled"}
                    </Typography>
                  )}
                </Box>
              </Tooltip>

              {/* Auto Scroll - Toggle */}
              <Tooltip
                title={
                  autoScroll
                    ? "Click to disable Auto Scroll"
                    : "Click to enable Auto Scroll"
                }
              >
                <Box
                  onClick={() => {
                    const newValue = !autoScroll;
                    setAutoScroll(newValue);
                    if (newValue) {
                      // Reset state when enabling auto-scroll
                      // The effect will handle scrolling when autoScroll changes
                      hasScrolledInitiallyRef.current = false;
                      setIsAtBottom(true);
                    }
                  }}
                  sx={menuRowSx()}
                >
                  <IconButton
                    size="small"
                    sx={menuIconSx(
                      autoScroll ? "primary.main" : "text.disabled"
                    )}
                  >
                    {autoScroll ? (
                      <VerticalAlignBottomIcon />
                    ) : (
                      <VerticalAlignBottomOutlinedIcon />
                    )}
                  </IconButton>
                  {!isMobile && (
                    <Typography
                      variant="body2"
                      sx={menuLabelSx(
                        autoScroll ? "primary.main" : "text.disabled"
                      )}
                    >
                      Auto Scroll
                    </Typography>
                  )}
                </Box>
              </Tooltip>

              {/* Scroll to Top */}
              <Tooltip title="Scroll to top">
                <Box onClick={scrollToTop} sx={menuRowSx()}>
                  <IconButton size="small" sx={menuIconSx("primary.main")}>
                    <VerticalAlignTopIcon />
                  </IconButton>
                  {!isMobile && (
                    <Typography
                      variant="body2"
                      sx={menuLabelSx("primary.main")}
                    >
                      Top
                    </Typography>
                  )}
                </Box>
              </Tooltip>

              {/* Auto Refresh - Toggles between disabled and 3s, also acts as refresh button */}
              <Tooltip
                title={
                  logLoading
                    ? "Loading logs..."
                    : refreshInterval
                    ? `Auto Refresh: ${refreshInterval}s (Click to disable or refresh)`
                    : "Click to enable Auto Refresh (3s) or refresh now"
                }
              >
                <Box
                  onClick={() => {
                    if (logLoading) return;

                    // Always fetch logs when clicked
                    fetchLogs();

                    // Toggle between: null (disabled) <-> 3s
                    if (refreshInterval === null) {
                      setRefreshInterval(3);
                    } else {
                      setRefreshInterval(null);
                    }
                  }}
                  sx={menuRowSx({
                    cursor: logLoading ? "not-allowed" : "pointer",
                    opacity: logLoading ? 0.6 : 1,
                    "&:hover": logLoading
                      ? {}
                      : isMobile
                      ? { opacity: 0.85 }
                      : { opacity: 0.9, backgroundColor: "action.hover" },
                  })}
                >
                  <IconButton
                    size="small"
                    sx={menuIconSx(
                      refreshInterval ? "primary.main" : "text.disabled"
                    )}
                    disabled={logLoading}
                  >
                    <AutorenewIcon />
                  </IconButton>
                  {!isMobile && (
                    <Typography
                      variant="body2"
                      sx={menuLabelSx(
                        refreshInterval ? "primary.main" : "text.disabled"
                      )}
                    >
                      {logLoading
                        ? "Loading..."
                        : `Auto Refresh${
                            refreshInterval ? ` (${refreshInterval}s)` : ""
                          }`}
                    </Typography>
                  )}
                </Box>
              </Tooltip>

              {/* Log Level - Filter */}
              <Tooltip title="Click to change log level">
                <Box onClick={handleLogLevelMenuOpen} sx={menuRowSx()}>
                  <IconButton size="small" sx={menuIconSx("primary.main")}>
                    <FilterListIcon />
                  </IconButton>
                  {!isMobile && (
                    <Typography
                      variant="body2"
                      sx={menuLabelSx("primary.main", {
                        textTransform: "capitalize",
                      })}
                    >
                      {logLevel}
                    </Typography>
                  )}
                </Box>
              </Tooltip>
              <Menu
                anchorEl={logLevelMenuAnchor}
                open={Boolean(logLevelMenuAnchor)}
                onClose={handleLogLevelMenuClose}
              >
                <MenuItem
                  onClick={() => handleLogLevelChange("access")}
                  selected={logLevel === "access"}
                >
                  Access
                </MenuItem>
                <MenuItem
                  onClick={() => handleLogLevelChange("debug")}
                  selected={logLevel === "debug"}
                >
                  Debug
                </MenuItem>
                <MenuItem
                  onClick={() => handleLogLevelChange("info")}
                  selected={logLevel === "info"}
                >
                  Info
                </MenuItem>
                <MenuItem
                  onClick={() => handleLogLevelChange("warning")}
                  selected={logLevel === "warning"}
                >
                  Warning
                </MenuItem>
                <MenuItem
                  onClick={() => handleLogLevelChange("error")}
                  selected={logLevel === "error"}
                >
                  Error
                </MenuItem>
              </Menu>

              {/* Line Wrap */}
              <Tooltip
                title={
                  lineWrap
                    ? "Disable line wrap (enable horizontal scrolling)"
                    : "Enable line wrap"
                }
              >
                <Box onClick={() => setLineWrap((v) => !v)} sx={menuRowSx()}>
                  <IconButton
                    size="small"
                    sx={menuIconSx(lineWrap ? "primary.main" : "text.disabled")}
                  >
                    {lineWrap ? <WrapTextIcon /> : <NotesIcon />}
                  </IconButton>
                  {!isMobile && (
                    <Typography
                      variant="body2"
                      sx={menuLabelSx(
                        lineWrap ? "primary.main" : "text.disabled"
                      )}
                    >
                      Line Wrap
                    </Typography>
                  )}
                </Box>
              </Tooltip>

              {/* View Logs */}
              <Tooltip title="Open logs in new tab">
                <Box onClick={handleViewLogs} sx={menuRowSx()}>
                  <IconButton size="small" sx={menuIconSx("primary.main")}>
                    <DescriptionIcon />
                  </IconButton>
                  {!isMobile && (
                    <Typography
                      variant="body2"
                      sx={menuLabelSx("primary.main")}
                    >
                      Log
                    </Typography>
                  )}
                </Box>
              </Tooltip>
            </Box>
          </CardContent>
        </Card>

        {/* Log Viewer */}
        <Card
          sx={{
            flex: 1,
            minWidth: 0,
            minHeight: 0,
            display: "flex",
            flexDirection: "column",
          }}
        >
          <CardContent
            sx={{
              flex: 1,
              minHeight: 0,
              display: "flex",
              flexDirection: "column",
              p: 1.5,
            }}
          >
            {logError && (
              <Alert severity="error" sx={{ mb: 2 }}>
                {logError}
              </Alert>
            )}

            <Paper
              ref={logContainerRef}
              onScroll={handleLogScroll}
              sx={{
                flex: 1,
                p: 1.5,
                bgcolor: "grey.900",
                color: "grey.100",
                fontFamily: "monospace",
                fontSize: "0.8rem",
                overflow: "auto",
                whiteSpace: lineWrap ? "pre-wrap" : "pre",
                wordBreak: lineWrap ? "break-word" : "normal",
                minHeight: 0, // Important for flex children
                textAlign: "left",
                direction: "ltr",
                position: "relative",
              }}
            >
              {logLoading && logs === "" ? (
                <Typography color="text.secondary">Loading logs...</Typography>
              ) : logs === "" ? (
                <Typography color="text.secondary">
                  No logs available. Click "Refresh" to fetch.
                </Typography>
              ) : (
                <>
                  {logs}
                  {/* Invisible sentinel element for Intersection Observer */}
                  <div
                    ref={scrollSentinelRef}
                    style={{ height: "1px", width: "100%" }}
                  />
                </>
              )}
            </Paper>
          </CardContent>
        </Card>
      </Box>
    </SettingsModal>
  );
}
