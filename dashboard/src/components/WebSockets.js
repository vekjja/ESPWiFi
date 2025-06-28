import React, { useState, useEffect, useRef, useMemo } from "react";
import {
  Container,
  Typography,
  TextField,
  FormControl,
  InputLabel,
  Select,
  MenuItem,
  CircularProgress,
  Button,
  Box,
  FormControlLabel,
  Checkbox,
} from "@mui/material";
import { RestartAlt, LinkOff } from "@mui/icons-material";
import DeleteButton from "./DeleteButton";
import SaveButton from "./SaveButton";
import IButton from "./IButton";
import Module from "./Module";
import SettingsModal from "./SettingsModal";

export default function WebSockets({ config, saveConfig }) {
  const [webSockets, setWebSockets] = useState([]);
  const [messages, setMessages] = useState([]);
  const [connectionStatus, setConnectionStatus] = useState({});
  const [openModal, setOpenModal] = useState(false);
  const [selectedWebSocket, setSelectedWebSocket] = useState(null);
  const [editedURL, setEditedURL] = useState("");
  const [editedName, setEditedName] = useState("");
  const [editedType, setEditedType] = useState("text");
  const [editedFontSize, setEditedFontSize] = useState(14);
  const [editedEnableSending, setEditedEnableSending] = useState(true);
  const [inputTexts, setInputTexts] = useState({});

  // Use refs to store WebSocket instances for proper cleanup
  const socketRefs = useRef({});
  const prevWebSocketConfigRef = useRef(null);

  // Create a stable reference for WebSocket configuration
  const webSocketConfig = useMemo(() => {
    if (config && config.webSockets) {
      return config.webSockets.map((ws) => ({
        ...ws,
        type: ws.type || "text",
        fontSize: ws.fontSize || 14,
        enableSending: ws.enableSending !== false,
      }));
    }
    return [];
  }, [config?.webSockets ? JSON.stringify(config.webSockets) : null]);

  useEffect(() => {
    // Only update if the configuration has actually changed
    const currentConfigString = JSON.stringify(webSockets);
    const newConfigString = JSON.stringify(webSocketConfig);

    if (currentConfigString !== newConfigString) {
      setWebSockets(webSocketConfig);
    }
  }, [webSocketConfig]);

  useEffect(() => {
    // Check if the WebSocket configuration has actually changed
    const currentConfigString = JSON.stringify(webSockets);
    const prevConfigString = prevWebSocketConfigRef.current;

    if (currentConfigString === prevConfigString) {
      console.log("WebSocket configuration unchanged, skipping reconnection");
      return;
    }

    console.log("WebSocket configuration changed, recreating connections");
    prevWebSocketConfigRef.current = currentConfigString;

    // Clean up existing connections first
    Object.values(socketRefs.current).forEach((ws) => {
      if (ws && ws.readyState !== WebSocket.CLOSED) {
        ws.close();
      }
    });
    socketRefs.current = {};

    // Clear messages when WebSockets change to prevent index mismatches
    setMessages([]);

    // Create new connections only if webSockets array has changed
    webSockets.forEach((webSocket, index) => {
      // Set loading state for this socket
      setConnectionStatus((prev) => ({
        ...prev,
        [index]: "connecting",
      }));

      const ws = new WebSocket(webSocket.url);
      if (webSocket.type === "binary") {
        ws.binaryType = "arraybuffer";
      }

      // Store the WebSocket instance in refs for cleanup
      socketRefs.current[index] = ws;

      ws.onopen = () => {
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "connected",
        }));
      };

      ws.onmessage = (event) => {
        if (webSocket.type === "binary") {
          const blob = new Blob([event.data], { type: "image/jpeg" });
          const objectURL = URL.createObjectURL(blob);
          setMessages((prev) => {
            const updated = [...prev];
            updated[index] = objectURL;
            return updated;
          });
        } else {
          setMessages((prev) => {
            const updated = [...prev];
            updated[index] = event.data;
            return updated;
          });
        }
      };

      ws.onerror = (error) => {
        console.error("WebSocket error:", error);
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "error",
        }));
      };

      ws.onclose = () => {
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "📶",
        }));
        // Only remove from refs if this is still the current WebSocket for this index
        if (socketRefs.current[index] === ws) {
          delete socketRefs.current[index];

          // Auto-reconnect after a timeout (only if not manually disconnected)
          setTimeout(() => {
            if (!socketRefs.current[index] && webSockets[index]) {
              console.log(`Auto-reconnecting to ${webSockets[index].url}`);
              setConnectionStatus((prev) => ({
                ...prev,
                [index]: "connecting",
              }));

              const newWs = new WebSocket(webSockets[index].url);
              if (webSockets[index].type === "binary") {
                newWs.binaryType = "arraybuffer";
              }

              socketRefs.current[index] = newWs;

              newWs.onopen = () => {
                setConnectionStatus((prev) => ({
                  ...prev,
                  [index]: "connected",
                }));
              };

              newWs.onmessage = (event) => {
                if (webSockets[index].type === "binary") {
                  const blob = new Blob([event.data], { type: "image/jpeg" });
                  const objectURL = URL.createObjectURL(blob);
                  setMessages((prev) => {
                    const updated = [...prev];
                    updated[index] = objectURL;
                    return updated;
                  });
                } else {
                  setMessages((prev) => {
                    const updated = [...prev];
                    updated[index] = event.data;
                    return updated;
                  });
                }
              };

              newWs.onerror = (error) => {
                console.error("WebSocket auto-reconnect error:", error);
                setConnectionStatus((prev) => ({
                  ...prev,
                  [index]: "error",
                }));
              };

              newWs.onclose = () => {
                setConnectionStatus((prev) => ({
                  ...prev,
                  [index]: "📶",
                }));
                if (socketRefs.current[index] === newWs) {
                  delete socketRefs.current[index];
                }
              };
            }
          }, 3000); // 3 second timeout for auto-reconnection
        }
      };
    });

    // Cleanup function
    return () => {
      // Clean up object URLs to prevent memory leaks
      setMessages((prev) => {
        prev.forEach((message, idx) => {
          if (typeof message === "string" && message.startsWith("blob:")) {
            URL.revokeObjectURL(message);
          }
        });
        return [];
      });

      Object.values(socketRefs.current).forEach((ws) => {
        if (ws && ws.readyState !== WebSocket.CLOSED) {
          ws.close();
        }
      });
      socketRefs.current = {};
    };
  }, [webSockets]); // Only recreate when webSockets array changes

  const handleSettingsClick = (webSocket) => {
    setSelectedWebSocket(webSocket);
    setOpenModal(true);
  };

  const handleCloseModal = () => {
    setOpenModal(false);
    setSelectedWebSocket(null);
  };

  const handleDeleteSocket = () => {
    const updatedWebSockets = webSockets.filter(
      (ws) => ws !== selectedWebSocket
    );
    setWebSockets(updatedWebSockets);

    // Update local config only - no immediate ESP32 save
    const updatedConfig = { ...config, webSockets: updatedWebSockets };
    saveConfig(updatedConfig);

    handleCloseModal();
  };

  const handleSaveSettings = () => {
    const updatedWebSockets = webSockets.map((ws) =>
      ws === selectedWebSocket
        ? {
            ...selectedWebSocket,
            url: editedURL,
            name: editedName,
            type: editedType,
            fontSize: Number(editedFontSize),
            enableSending: editedEnableSending,
          }
        : ws
    );
    setWebSockets(updatedWebSockets);

    // Update local config only - no immediate ESP32 save
    const updatedConfig = { ...config, webSockets: updatedWebSockets };
    saveConfig(updatedConfig);

    handleCloseModal();
  };

  const handleSend = (index) => {
    const text = inputTexts[index] || "";
    if (
      text.trim() &&
      socketRefs.current[index] &&
      socketRefs.current[index].readyState === WebSocket.OPEN
    ) {
      socketRefs.current[index].send(text);
      // Clear the input after sending
      setInputTexts((prev) => ({
        ...prev,
        [index]: "",
      }));
    }
  };

  const handleInputChange = (index, value) => {
    setInputTexts((prev) => ({
      ...prev,
      [index]: value,
    }));
  };

  const handleKeyPress = (index, event) => {
    if (event.key === "Enter") {
      handleSend(index);
    }
  };

  const handleReconnect = () => {
    if (selectedWebSocket) {
      // Find the index of the selected WebSocket
      const index = webSockets.findIndex((ws) => ws === selectedWebSocket);
      if (index !== -1) {
        // Close the current connection
        if (
          socketRefs.current[index] &&
          socketRefs.current[index].readyState !== WebSocket.CLOSED
        ) {
          socketRefs.current[index].close();
        }

        // Set connecting status
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "connecting",
        }));

        // Add timeout before reconnection attempt
        setTimeout(() => {
          // Create new WebSocket connection
          const ws = new WebSocket(selectedWebSocket.url);
          if (selectedWebSocket.type === "binary") {
            ws.binaryType = "arraybuffer";
          }

          // Store the new WebSocket instance
          socketRefs.current[index] = ws;

          ws.onopen = () => {
            setConnectionStatus((prev) => ({
              ...prev,
              [index]: "connected",
            }));
          };

          ws.onmessage = (event) => {
            if (selectedWebSocket.type === "binary") {
              const blob = new Blob([event.data], { type: "image/jpeg" });
              const objectURL = URL.createObjectURL(blob);
              setMessages((prev) => {
                const updated = [...prev];
                updated[index] = objectURL;
                return updated;
              });
            } else {
              setMessages((prev) => {
                const updated = [...prev];
                updated[index] = event.data;
                return updated;
              });
            }
          };

          ws.onerror = (error) => {
            console.error("WebSocket error:", error);
            setConnectionStatus((prev) => ({
              ...prev,
              [index]: "error",
            }));
          };

          ws.onclose = () => {
            setConnectionStatus((prev) => ({
              ...prev,
              [index]: "📶",
            }));
            // Only remove from refs if this is still the current WebSocket for this index
            if (socketRefs.current[index] === ws) {
              delete socketRefs.current[index];
            }
          };
        }, 1000); // 1 second timeout before reconnection
      }
    }

    // Close the modal after initiating reconnection
    handleCloseModal();
  };

  const handleDisconnect = () => {
    if (selectedWebSocket) {
      // Find the index of the selected WebSocket
      const index = webSockets.findIndex((ws) => ws === selectedWebSocket);
      if (index !== -1) {
        // Close the current connection
        if (
          socketRefs.current[index] &&
          socketRefs.current[index].readyState !== WebSocket.CLOSED
        ) {
          socketRefs.current[index].close();
        }

        // Set disconnected status
        setConnectionStatus((prev) => ({
          ...prev,
          [index]: "disconnected",
        }));
      }
    }

    // Close the modal after disconnecting
    handleCloseModal();
  };

  useEffect(() => {
    if (selectedWebSocket) {
      setEditedURL(selectedWebSocket.url);
      setEditedName(selectedWebSocket.name || "");
      setEditedType(selectedWebSocket.type || "text");
      setEditedFontSize(selectedWebSocket.fontSize || 14);
      setEditedEnableSending(selectedWebSocket.enableSending !== false);
    }
  }, [selectedWebSocket]);

  return (
    <Container
      sx={{
        marginTop: 2,
        display: "flex",
        flexWrap: "wrap",
        justifyContent: "center",
      }}
    >
      {webSockets.map((webSocket, index) => (
        <Module
          key={index}
          title={webSocket.name || "Unnamed"}
          onSettings={() => handleSettingsClick(webSocket)}
          settingsTooltip={"Websocket Settings"}
        >
          {connectionStatus[index] === "connecting" ? (
            <div
              style={{
                display: "flex",
                justifyContent: "center",
                alignItems: "center",
                height: "120px",
                flexDirection: "column",
              }}
            >
              <CircularProgress size={24} sx={{ marginBottom: 1 }} />
              <Typography variant="caption" color="text.secondary">
                Connecting...
              </Typography>
            </div>
          ) : connectionStatus[index] === "error" ? (
            <div
              style={{
                display: "flex",
                justifyContent: "center",
                alignItems: "center",
                height: "120px",
                flexDirection: "column",
              }}
            >
              <Typography variant="caption" color="error">
                Connection Error
              </Typography>
            </div>
          ) : connectionStatus[index] === "disconnected" ? (
            <div
              style={{
                display: "flex",
                justifyContent: "center",
                alignItems: "center",
                height: "120px",
                flexDirection: "column",
              }}
            >
              <Typography variant="caption" color="text.secondary">
                Disconnected
              </Typography>
            </div>
          ) : webSocket.type === "binary" ? (
            messages[index] &&
            typeof messages[index] === "string" &&
            messages[index].startsWith("blob:") ? (
              <img
                src={messages[index]}
                alt="WebSocket"
                style={{
                  width: "100%",
                  marginTop: "10px",
                  maxHeight: "120px",
                  objectFit: "contain",
                }}
              />
            ) : (
              <Typography variant="caption" color="text.secondary">
                Waiting for image...
              </Typography>
            )
          ) : (
            <Typography
              variant="body2"
              sx={{
                marginTop: "10px",
                wordBreak: "break-all",
                maxHeight: "120px",
                overflow: "auto",
                textAlign: "center",
                fontSize: webSocket.fontSize || 14,
              }}
            >
              {typeof messages[index] === "string" ? messages[index] : ""}
            </Typography>
          )}

          {/* Text input for sending messages - only show when connected and sending is enabled */}
          {connectionStatus[index] === "connected" &&
            webSocket.enableSending !== false && (
              <Box sx={{ marginTop: 2, display: "flex", gap: 1 }}>
                <TextField
                  label="Send message"
                  value={inputTexts[index] || ""}
                  onChange={(e) => handleInputChange(index, e.target.value)}
                  onKeyPress={(e) => handleKeyPress(index, e)}
                  variant="outlined"
                  size="small"
                  fullWidth
                  placeholder="Type a message..."
                />
                <Button
                  onClick={() => handleSend(index)}
                  variant="contained"
                  size="small"
                  disabled={!inputTexts[index] || !inputTexts[index].trim()}
                >
                  Send
                </Button>
              </Box>
            )}
        </Module>
      ))}

      <SettingsModal
        open={openModal}
        onClose={handleCloseModal}
        title="WebSocket Settings"
        actions={
          <>
            <DeleteButton
              onClick={handleDeleteSocket}
              tooltip={"Delete Websocket"}
            />
            <IButton
              onClick={handleReconnect}
              tooltip={"Reconnect WebSocket"}
              Icon={RestartAlt}
            />
            <IButton
              onClick={handleDisconnect}
              tooltip={"Disconnect WebSocket"}
              Icon={LinkOff}
            />
            <SaveButton
              onClick={handleSaveSettings}
              tooltip={"Apply Websocket Settings"}
            />
          </>
        }
      >
        <TextField
          label="Name"
          value={editedName}
          onChange={(e) => setEditedName(e.target.value)}
          variant="outlined"
          fullWidth
          sx={{ marginBottom: 2 }}
        />
        <TextField
          label="WebSocket URL"
          value={editedURL}
          onChange={(e) => setEditedURL(e.target.value)}
          variant="outlined"
          fullWidth
          sx={{ marginBottom: 2 }}
          InputProps={{
            startAdornment: (
              <span style={{ color: "#666", marginRight: "8px" }}></span>
            ),
          }}
        />
        <FormControl fullWidth sx={{ marginBottom: 2 }}>
          <InputLabel id="websocket-type-select-label">Type</InputLabel>
          <Select
            labelId="websocket-type-select-label"
            id="websocket-type-select"
            value={editedType}
            label="Type"
            onChange={(e) => setEditedType(e.target.value)}
          >
            <MenuItem value="binary">Binary</MenuItem>
            <MenuItem value="text">Text</MenuItem>
          </Select>
        </FormControl>
        <TextField
          label="Font Size (px)"
          type="number"
          value={editedFontSize}
          onChange={(e) => setEditedFontSize(e.target.value)}
          variant="outlined"
          fullWidth
          sx={{ marginBottom: 2 }}
        />
        <FormControlLabel
          control={
            <Checkbox
              checked={editedEnableSending}
              onChange={(e) => setEditedEnableSending(e.target.checked)}
            />
          }
          label="Enable Sending"
        />
      </SettingsModal>
    </Container>
  );
}
