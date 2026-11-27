import React, { useState, useEffect, useRef } from "react";
import { Typography, TextField, Button, Box } from "@mui/material";
import RestartAltIcon from "@mui/icons-material/RestartAlt";
import LinkIcon from "@mui/icons-material/Link";
import LinkOffIcon from "@mui/icons-material/LinkOff";
import Module from "./Module";
import WebSocketSettingsModal from "./WebSocketSettingsModal";
import { buildWebSocketUrl } from "../utils/apiUtils";
import { createSocketSession } from "../utils/socketSession";
import { connectionLabel } from "./moduleConfig";

function normalizeWsUrl(raw) {
  let wsUrl = String(raw || "").trim();
  if (!wsUrl) return "";
  if (wsUrl.startsWith("/")) {
    wsUrl = buildWebSocketUrl(wsUrl);
  } else if (!wsUrl.startsWith("ws://") && !wsUrl.startsWith("wss://")) {
    wsUrl = `ws://${wsUrl}`;
  }
  try {
    new URL(wsUrl);
  } catch {
    return "";
  }
  return wsUrl;
}

export default function WebSocketModule({
  index,
  initialProps,
  onUpdate,
  onDelete,
}) {
  const moduleKey = initialProps.key || index;

  const [message, setMessage] = useState("");
  const [connectionStatus, setConnectionStatus] = useState("disconnected");
  const [openModal, setOpenModal] = useState(false);
  const [inputText, setInputText] = useState("");

  const [websocketSettingsData, setWebSocketSettingsData] = useState({
    name: initialProps.name || "",
    url: initialProps.url || "",
    payload: initialProps.payload || "text",
    fontSize: initialProps.fontSize || 14,
    enableSending: initialProps.enableSending !== false,
    imageRotation: initialProps.imageRotation || 0,
    size: initialProps.size || "medium",
    width: initialProps.width || 240,
    height: initialProps.height || 240,
  });

  const sessionRef = useRef(null);
  const messageRef = useRef("");
  const urlRef = useRef(initialProps.url || "");
  const payloadRef = useRef(initialProps.payload || "text");
  const desiredRef = useRef(initialProps.connectionState === "connected");
  const skipUrlRestart = useRef(true);

  urlRef.current = initialProps.url || "";
  payloadRef.current = initialProps.payload || "text";

  const revokeMessageUrl = () => {
    if (
      typeof messageRef.current === "string" &&
      messageRef.current.startsWith("blob:")
    ) {
      URL.revokeObjectURL(messageRef.current);
    }
  };

  useEffect(() => {
    const session = createSocketSession({
      getUrl: () => normalizeWsUrl(urlRef.current),
      prepare: (ws) => {
        if (payloadRef.current === "binary") ws.binaryType = "arraybuffer";
      },
      onStatus: (status) => setConnectionStatus(status),
      onMessage: (event) => {
        if (payloadRef.current === "binary") {
          const blob = new Blob([event.data], { type: "image/jpeg" });
          const objectURL = URL.createObjectURL(blob);
          revokeMessageUrl();
          messageRef.current = objectURL;
          setMessage(objectURL);
        } else {
          messageRef.current = event.data;
          setMessage(event.data);
        }
      },
    });
    sessionRef.current = session;

    if (desiredRef.current) session.start();

    return () => {
      session.dispose();
      sessionRef.current = null;
      revokeMessageUrl();
    };
  }, [moduleKey]);

  // A saved URL or payload change should reopen the socket the user asked to keep up.
  useEffect(() => {
    if (skipUrlRestart.current) {
      skipUrlRestart.current = false;
      return;
    }
    if (desiredRef.current) sessionRef.current?.start();
  }, [initialProps.url, initialProps.payload]);

  const handleSettingsClick = () => {
    setWebSocketSettingsData({
      name: initialProps.name || "",
      url: initialProps.url || "",
      payload: initialProps.payload || "text",
      fontSize: initialProps.fontSize || 14,
      enableSending: initialProps.enableSending !== false,
      imageRotation: initialProps.imageRotation || 0,
      size: initialProps.size || "medium",
      width: initialProps.width || 240,
      height: initialProps.height || 240,
    });
    setOpenModal(true);
  };

  const handleCloseModal = () => {
    setOpenModal(false);
  };

  const handleWebSocketDataChange = (changes) => {
    setWebSocketSettingsData((prev) => ({ ...prev, ...changes }));
  };

  const handleDeleteSocket = () => {
    desiredRef.current = false;
    sessionRef.current?.stop();
    onDelete(moduleKey);
    handleCloseModal();
  };

  const handleSaveSettings = () => {
    onUpdate(moduleKey, {
      type: initialProps.type,
      key: initialProps.key,
      url: websocketSettingsData.url,
      name: websocketSettingsData.name,
      payload: websocketSettingsData.payload,
      fontSize: Number(websocketSettingsData.fontSize),
      enableSending: websocketSettingsData.enableSending,
      imageRotation: Number(websocketSettingsData.imageRotation),
      connectionState: desiredRef.current ? "connected" : "disconnected",
      size: websocketSettingsData.size,
      width: websocketSettingsData.width,
      height: websocketSettingsData.height,
    });
    handleCloseModal();
  };

  const handleConnect = () => {
    desiredRef.current = true;
    sessionRef.current?.start();
    onUpdate(moduleKey, { connectionState: "connected" });
  };

  const handleDisconnect = () => {
    desiredRef.current = false;
    sessionRef.current?.stop();
    onUpdate(moduleKey, { connectionState: "disconnected" });
  };

  const handleSend = () => {
    const socket = sessionRef.current?.getSocket();
    if (!inputText.trim() || !socket) return;
    socket.send(inputText);
    setInputText("");
  };

  const handleKeyPress = (event) => {
    if (event.key === "Enter") handleSend();
  };

  const connected = connectionStatus === "connected";
  const busy =
    connectionStatus === "connecting" || connectionStatus === "reconnecting";

  return (
    <Module
      title={initialProps.name || "WebSocket" + moduleKey}
      moduleConfig={{
        ...initialProps,
        size: websocketSettingsData.size,
        width: websocketSettingsData.width,
        height: websocketSettingsData.height,
      }}
      connectionStatus={connectionStatus}
      onSettings={handleSettingsClick}
      settingsTooltip="WebSocket Settings"
      onReconnect={connected ? handleDisconnect : handleConnect}
      reconnectTooltip={
        connected ? "Disconnect" : busy ? connectionLabel(connectionStatus) : "Connect"
      }
      reconnectIcon={connected ? LinkIcon : busy ? RestartAltIcon : LinkOffIcon}
      reconnectColor={connected ? "primary" : "error"}
    >
      {connected ? (
        initialProps.payload === "binary" ? (
          messageRef.current &&
          typeof messageRef.current === "string" &&
          messageRef.current.startsWith("blob:") ? (
            <img
              src={message}
              alt="WebSocket"
              style={{
                width: "100%",
                marginTop: "10px",
                maxHeight: "120px",
                objectFit: "contain",
                transform: `rotate(${initialProps.imageRotation || 0}deg)`,
              }}
            />
          ) : (
            <Typography variant="caption" color="text.secondary">
              Waiting for image...
            </Typography>
          )
        ) : (
          <Box
            sx={{
              display: "flex",
              alignItems: "center",
              justifyContent: "center",
              minHeight: "60px",
            }}
          >
            <Typography
              variant="body2"
              sx={{
                wordBreak: "break-all",
                textAlign: "center",
                fontSize: initialProps.fontSize || 14,
                maxHeight: "120px",
                overflow: "auto",
              }}
            >
              {typeof message === "string" ? message : ""}
            </Typography>
          </Box>
        )
      ) : (
        <Box
          sx={{
            display: "flex",
            justifyContent: "center",
            alignItems: "center",
            height: "120px",
          }}
        >
          <Typography
            variant="caption"
            color={
              connectionStatus === "error" || connectionStatus === "offline"
                ? "error"
                : "text.secondary"
            }
          >
            {connectionLabel(connectionStatus)}
          </Typography>
        </Box>
      )}

      {connected && initialProps.enableSending !== false && (
        <Box
          sx={{ mt: 1, mb: 1, px: 1, display: "flex", gap: 1, width: "100%" }}
        >
          <TextField
            label="Send message"
            value={inputText}
            onChange={(e) => setInputText(e.target.value)}
            onKeyDown={handleKeyPress}
            variant="outlined"
            size="small"
            fullWidth
            placeholder="Type a message..."
          />
          <Button
            onClick={handleSend}
            variant="contained"
            size="small"
            disabled={!inputText || !inputText.trim()}
          >
            Send
          </Button>
        </Box>
      )}

      <WebSocketSettingsModal
        open={openModal}
        onClose={handleCloseModal}
        onSave={handleSaveSettings}
        onDelete={handleDeleteSocket}
        websocketData={websocketSettingsData}
        onWebSocketDataChange={handleWebSocketDataChange}
      />
    </Module>
  );
}
