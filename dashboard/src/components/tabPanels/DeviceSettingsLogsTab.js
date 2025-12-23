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
import FilterListIcon from "@mui/icons-material/FilterList";
import { buildApiUrl, getFetchOptions } from "../../utils/apiUtils";

export default function DeviceSettingsLogsTab({ config, saveConfigToDevice }) {
  const theme = useTheme();
  const isMobile = useMediaQuery(theme.breakpoints.down("sm"));

  const [logs, setLogs] = useState("");
  const [logLoading, setLogLoading] = useState(false);
  const [logError, setLogError] = useState("");
  const [logEnabled, setLogEnabled] = useState(config?.log?.enabled !== false);
  const [logLevel, setLogLevel] = useState(config?.log?.level || "info");
  // Auto refresh interval: null (disabled) or 3 seconds
  const [refreshInterval, setRefreshInterval] = useState(null);
  const [autoScroll, setAutoScroll] = useState(true);
  const [logLevelMenuAnchor, setLogLevelMenuAnchor] = useState(null);
  const logContainerRef = useRef(null);
  const refreshIntervalRef = useRef(null);
  const scrollSentinelRef = useRef(null);
  const observerRef = useRef(null);
  const hasScrolledInitiallyRef = useRef(false);
  const prevAutoScrollRef = useRef(autoScroll);
  const [isAtBottom, setIsAtBottom] = useState(true);

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
    const fetchUrl = buildApiUrl("/log");
    setLogLoading(true);
    setLogError("");

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 10000);

    try {
      const response = await fetch(
        fetchUrl,
        getFetchOptions({
          signal: controller.signal,
        })
      );

      clearTimeout(timeoutId);

      if (!response.ok) {
        if (response.status === 404) {
          setLogs(
            "Log file not found. Logging may be disabled or the file hasn't been created yet."
          );
          setLogError("");
        } else {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
      } else {
        const text = await response.text();
        setLogs(text);
        setLogError("");
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
      const validLevels = ["debug", "info", "warning", "error"];
      if (level && !validLevels.includes(level)) {
        throw new Error(
          "Invalid log level. Must be: debug, info, warning, or error"
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
      console.log("Log settings updated");
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

  // Handle view logs in new window
  const handleViewLogs = () => {
    const apiUrl = config?.apiURL || buildApiUrl("");
    window.open(`${apiUrl}/log`, "_blank");
  };

  // Initial fetch on mount
  useEffect(() => {
    fetchLogs();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Update local state when config changes
  useEffect(() => {
    if (config) {
      setLogEnabled(config.log?.enabled !== false);
      setLogLevel(config.log?.level || "info");
    }
  }, [config]);

  return (
    <Box
      sx={{
        display: "flex",
        flexDirection: "column",
        height: "100%",
        mt: -3,
      }}
    >
      {/* Controls Row - Fixed at top */}
      <Card
        sx={{
          position: "sticky",
          top: -24, // Offset for TabPanel's pt: 3 (3 * 8px = 24px)
          zIndex: 10,
          mb: 1,
          bgcolor: "background.paper",
          boxShadow: 2,
        }}
      >
        <CardContent sx={{ py: isMobile ? 1.5 : 2, px: isMobile ? 2 : 3 }}>
          <Box
            sx={{
              display: "flex",
              flexDirection: "column",
              gap: 2,
            }}
          >
            {/* Top Row: Enable and Auto Options */}
            <Box
              sx={{
                display: "flex",
                flexDirection: isMobile ? "column" : "row",
                flexWrap: "wrap",
                alignItems: "center",
                gap: 2,
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
                <Box
                  onClick={handleLogEnabledChange}
                  sx={{
                    display: "flex",
                    alignItems: "center",
                    gap: 1,
                    cursor: "pointer",
                    "&:hover": {
                      opacity: 0.8,
                    },
                  }}
                >
                  <IconButton
                    sx={{
                      color: logEnabled ? "primary.main" : "text.disabled",
                      pointerEvents: "none",
                    }}
                  >
                    {logEnabled ? <PowerSettingsNewIcon /> : <PowerOffIcon />}
                  </IconButton>
                  <Typography
                    variant="body2"
                    sx={{
                      color: logEnabled ? "primary.main" : "text.disabled",
                      pointerEvents: "none",
                    }}
                  >
                    {logEnabled ? "Logging Enabled" : "Logging Disabled"}
                  </Typography>
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
                  sx={{
                    display: "flex",
                    alignItems: "center",
                    gap: 1,
                    cursor: "pointer",
                    "&:hover": {
                      opacity: 0.8,
                    },
                  }}
                >
                  <IconButton
                    sx={{
                      color: autoScroll ? "primary.main" : "text.disabled",
                      pointerEvents: "none",
                    }}
                  >
                    {autoScroll ? (
                      <VerticalAlignBottomIcon />
                    ) : (
                      <VerticalAlignBottomOutlinedIcon />
                    )}
                  </IconButton>
                  <Typography
                    variant="body2"
                    sx={{
                      color: autoScroll ? "primary.main" : "text.disabled",
                      pointerEvents: "none",
                    }}
                  >
                    Auto Scroll
                  </Typography>
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
                  sx={{
                    display: "flex",
                    alignItems: "center",
                    gap: 1,
                    cursor: logLoading ? "not-allowed" : "pointer",
                    opacity: logLoading ? 0.6 : 1,
                    "&:hover": logLoading
                      ? {}
                      : {
                          opacity: 0.8,
                        },
                  }}
                >
                  <IconButton
                    sx={{
                      color: refreshInterval ? "primary.main" : "text.disabled",
                      pointerEvents: "none",
                    }}
                    disabled={logLoading}
                  >
                    <AutorenewIcon />
                  </IconButton>
                  <Typography
                    variant="body2"
                    sx={{
                      color: refreshInterval ? "primary.main" : "text.disabled",
                      pointerEvents: "none",
                    }}
                  >
                    {logLoading
                      ? "Loading..."
                      : `Auto Refresh${
                          refreshInterval ? ` (${refreshInterval}s)` : ""
                        }`}
                  </Typography>
                </Box>
              </Tooltip>
            </Box>

            {/* Bottom Row: Other Options */}
            <Box
              sx={{
                display: "flex",
                flexDirection: isMobile ? "column" : "row",
                flexWrap: "wrap",
                alignItems: "center",
                gap: 2,
              }}
            >
              {/* Log Level - Filter */}
              <Tooltip title="Click to change log level">
                <Box
                  onClick={handleLogLevelMenuOpen}
                  sx={{
                    display: "flex",
                    alignItems: "center",
                    gap: 1,
                    cursor: "pointer",
                    "&:hover": {
                      opacity: 0.8,
                    },
                  }}
                >
                  <IconButton
                    sx={{
                      color: "primary.main",
                      pointerEvents: "none",
                    }}
                  >
                    <FilterListIcon />
                  </IconButton>
                  <Typography
                    variant="body2"
                    sx={{
                      color: "primary.main",
                      pointerEvents: "none",
                      textTransform: "capitalize",
                    }}
                  >
                    Log Level: {logLevel}
                  </Typography>
                </Box>
              </Tooltip>
              <Menu
                anchorEl={logLevelMenuAnchor}
                open={Boolean(logLevelMenuAnchor)}
                onClose={handleLogLevelMenuClose}
              >
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

              {/* View Logs */}
              <Tooltip title="Open logs in new tab">
                <Box
                  onClick={handleViewLogs}
                  sx={{
                    display: "flex",
                    alignItems: "center",
                    gap: 1,
                    cursor: "pointer",
                    "&:hover": {
                      opacity: 0.8,
                    },
                  }}
                >
                  <IconButton
                    sx={{
                      color: "primary.main",
                      pointerEvents: "none",
                    }}
                  >
                    <DescriptionIcon />
                  </IconButton>
                  <Typography
                    variant="body2"
                    sx={{
                      color: "primary.main",
                      pointerEvents: "none",
                    }}
                  >
                    Open in new tab
                  </Typography>
                </Box>
              </Tooltip>
            </Box>
          </Box>
        </CardContent>
      </Card>

      {/* Log Viewer */}
      <Card sx={{ flex: 1, display: "flex", flexDirection: "column" }}>
        <CardContent
          sx={{ flex: 1, display: "flex", flexDirection: "column", p: 2 }}
        >
          <Typography variant="h6" gutterBottom>
            Log Output
          </Typography>

          {logError && (
            <Alert severity="error" sx={{ mb: 2 }}>
              {logError}
            </Alert>
          )}

          <Paper
            ref={logContainerRef}
            sx={{
              flex: 1,
              p: 2,
              bgcolor: "grey.900",
              color: "grey.100",
              fontFamily: "monospace",
              fontSize: "0.875rem",
              overflow: "auto",
              whiteSpace: "pre-wrap",
              wordBreak: "break-word",
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
  );
}
