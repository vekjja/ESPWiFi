import React, { useState, useEffect, useRef, useCallback } from "react";
import {
  Box,
  Alert,
  Typography,
  Card,
  CardContent,
  FormControl,
  FormLabel,
  Select,
  MenuItem,
  Switch,
  Button,
  TextField,
  Paper,
} from "@mui/material";
import RefreshIcon from "@mui/icons-material/Refresh";
import DescriptionIcon from "@mui/icons-material/Description";
import { buildApiUrl, getFetchOptions } from "../../utils/apiUtils";
import IButton from "../IButton";

export default function DeviceSettingsLogsTab({ config, saveConfigToDevice }) {
  const [logs, setLogs] = useState("");
  const [logLoading, setLogLoading] = useState(false);
  const [logError, setLogError] = useState("");
  const [logEnabled, setLogEnabled] = useState(config?.log?.enabled !== false);
  const [logLevel, setLogLevel] = useState(config?.log?.level || "info");
  const [autoRefresh, setAutoRefresh] = useState(false);
  const [autoScroll, setAutoScroll] = useState(true);
  const [refreshInterval, setRefreshInterval] = useState(2); // seconds
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
    if (autoRefresh && refreshInterval > 0) {
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
  }, [autoRefresh, refreshInterval]);

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
  const handleLogEnabledChange = (event) => {
    const newValue = event.target.checked;
    setLogEnabled(newValue);
    // Update immediately
    updateLogSettings(newValue, logLevel);
  };

  // Handle log level change
  const handleLogLevelChange = (event) => {
    const newValue = event.target.value;
    setLogLevel(newValue);
    // Update immediately
    updateLogSettings(logEnabled, newValue);
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
        <CardContent sx={{ py: 2, px: 3 }}>
          <Box
            sx={{
              display: "flex",
              justifyContent: "space-between",
              alignItems: "flex-start",
              gap: 4,
            }}
          >
            {/* Toggle Options on the Left - Stacked in rows */}
            <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
              {/* Enable/Disable Logging */}
              <Box sx={{ display: "flex", alignItems: "center", gap: 2 }}>
                <FormLabel sx={{ whiteSpace: "nowrap", minWidth: 130 }}>
                  Enable Logging
                </FormLabel>
                <Switch
                  checked={logEnabled}
                  onChange={handleLogEnabledChange}
                  size="small"
                />
              </Box>

              {/* Auto Refresh */}
              <Box
                sx={{
                  display: "flex",
                  alignItems: "center",
                  gap: 2,
                  flexWrap: "wrap",
                }}
              >
                <FormLabel sx={{ whiteSpace: "nowrap", minWidth: 130 }}>
                  Auto Refresh
                </FormLabel>
                <Switch
                  checked={autoRefresh}
                  onChange={(e) => setAutoRefresh(e.target.checked)}
                  size="small"
                />
                {autoRefresh && (
                  <TextField
                    type="number"
                    label="Interval (sec)"
                    value={refreshInterval}
                    onChange={(e) =>
                      setRefreshInterval(
                        Math.max(1, parseInt(e.target.value) || 1)
                      )
                    }
                    size="small"
                    inputProps={{ min: 1, max: 60 }}
                    sx={{ width: 130, ml: 1 }}
                  />
                )}
              </Box>

              {/* Auto Scroll */}
              <Box sx={{ display: "flex", alignItems: "center", gap: 2 }}>
                <FormLabel sx={{ whiteSpace: "nowrap", minWidth: 130 }}>
                  Auto Scroll
                </FormLabel>
                <Switch
                  checked={autoScroll}
                  onChange={(e) => {
                    const newValue = e.target.checked;
                    setAutoScroll(newValue);
                    if (newValue) {
                      // Reset state when enabling auto-scroll
                      // The effect will handle scrolling when autoScroll changes
                      hasScrolledInitiallyRef.current = false;
                      setIsAtBottom(true);
                    }
                  }}
                  size="small"
                />
              </Box>
            </Box>

            {/* Controls on the Right - Stacked vertically */}
            <Box sx={{ display: "flex", flexDirection: "column", gap: 2 }}>
              {/* Log Level */}
              <FormControl size="small" sx={{ minWidth: 120 }}>
                <FormLabel sx={{ mb: 0.5, fontSize: "0.875rem" }}>
                  Log Level
                </FormLabel>
                <Select
                  value={logLevel}
                  onChange={handleLogLevelChange}
                  size="small"
                >
                  <MenuItem value="debug">Debug</MenuItem>
                  <MenuItem value="info">Info</MenuItem>
                  <MenuItem value="warning">Warning</MenuItem>
                  <MenuItem value="error">Error</MenuItem>
                </Select>
              </FormControl>

              {/* Refresh Button and View Logs Icon */}
              <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
                <Button
                  variant="contained"
                  startIcon={<RefreshIcon />}
                  onClick={fetchLogs}
                  disabled={logLoading}
                  size="small"
                >
                  {logLoading ? "Loading..." : "Refresh"}
                </Button>
                <IButton
                  Icon={DescriptionIcon}
                  onClick={handleViewLogs}
                  tooltip="View Logs"
                />
              </Box>
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
