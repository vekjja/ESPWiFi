import React, { useState, useEffect, useRef } from "react";
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
import { buildApiUrl, getFetchOptions } from "../../utils/apiUtils";

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
  const userScrolledUpRef = useRef(false);
  const isScrollingProgrammaticallyRef = useRef(false);

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

  // Handle scroll events to detect if user scrolled up manually
  useEffect(() => {
    const container = logContainerRef.current;
    if (!container || !autoScroll) return;

    let scrollTimeout;
    const handleScroll = () => {
      // Ignore programmatic scrolling
      if (isScrollingProgrammaticallyRef.current) {
        isScrollingProgrammaticallyRef.current = false;
        return;
      }

      // Clear any pending timeout
      clearTimeout(scrollTimeout);

      // Set a timeout to check scroll position after scrolling stops
      scrollTimeout = setTimeout(() => {
        if (!container || !autoScroll) return;

        const { scrollTop, scrollHeight, clientHeight } = container;
        const distanceFromBottom = scrollHeight - scrollTop - clientHeight;
        // If user is more than 50px from bottom, they scrolled up manually
        userScrolledUpRef.current = distanceFromBottom > 50;
      }, 100); // Wait 100ms after scroll stops
    };

    container.addEventListener("scroll", handleScroll, { passive: true });
    return () => {
      container.removeEventListener("scroll", handleScroll);
      clearTimeout(scrollTimeout);
    };
  }, [autoScroll]);

  // Auto-scroll to bottom when logs update
  useEffect(() => {
    if (!autoScroll || !logContainerRef.current) return;

    // Only auto-scroll if user hasn't manually scrolled up
    if (userScrolledUpRef.current) {
      return;
    }

    // Simple, direct scroll approach
    const scrollToBottom = () => {
      const container = logContainerRef.current;
      if (!container || !autoScroll || userScrolledUpRef.current) return;

      // Mark as programmatic scroll
      isScrollingProgrammaticallyRef.current = true;

      // Scroll to bottom
      container.scrollTop = container.scrollHeight;

      // Reset flag after a short delay
      setTimeout(() => {
        isScrollingProgrammaticallyRef.current = false;
      }, 300);
    };

    // Use multiple strategies to ensure scroll happens
    // Immediate attempt
    scrollToBottom();

    // Also try after a short delay to catch any DOM updates
    const timeout1 = setTimeout(scrollToBottom, 100);

    // And after requestAnimationFrame
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        scrollToBottom();
      });
    });

    return () => {
      clearTimeout(timeout1);
    };
  }, [logs, autoScroll]);

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
    const updateUrl = buildApiUrl("/api/log");

    try {
      const response = await fetch(
        updateUrl,
        getFetchOptions({
          method: "PUT",
          body: JSON.stringify({
            enabled: enabled,
            level: level,
          }),
        })
      );

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      const data = await response.json();
      console.log("Log settings updated:", data);

      // Backend already saved the config, no need to save again
      // The local state will be updated via the useEffect that watches config changes
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

  // Initial fetch on mount
  useEffect(() => {
    fetchLogs();
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
                      // Reset scroll state when enabling auto-scroll
                      userScrolledUpRef.current = false;
                      // Scroll to bottom immediately
                      setTimeout(() => {
                        if (logContainerRef.current) {
                          isScrollingProgrammaticallyRef.current = true;
                          logContainerRef.current.scrollTop =
                            logContainerRef.current.scrollHeight;
                        }
                      }, 0);
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

              {/* Manual Refresh Button */}
              <Button
                variant="contained"
                startIcon={<RefreshIcon />}
                onClick={fetchLogs}
                disabled={logLoading}
                size="small"
                sx={{ alignSelf: "flex-start" }}
              >
                {logLoading ? "Loading..." : "Refresh"}
              </Button>
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
            }}
          >
            {logLoading && logs === "" ? (
              <Typography color="text.secondary">Loading logs...</Typography>
            ) : logs === "" ? (
              <Typography color="text.secondary">
                No logs available. Click "Refresh" to fetch.
              </Typography>
            ) : (
              logs
            )}
          </Paper>
        </CardContent>
      </Card>
    </Box>
  );
}
