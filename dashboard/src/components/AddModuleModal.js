import React, { useState } from "react";
import {
  Dialog,
  DialogTitle,
  DialogContent,
  Tabs,
  Tab,
  Box,
  IconButton,
} from "@mui/material";
import CloseIcon from "@mui/icons-material/Close";
import PinSettingsModal from "./PinSettingsModal";
import WebSocketSettingsModal from "./WebSocketSettingsModal";

export default function AddModuleModal({
  open,
  onClose,
  onSavePin,
  onSaveWebSocket,
  pinData,
  webSocketData,
  onPinDataChange,
  onWebSocketDataChange,
}) {
  const [activeTab, setActiveTab] = useState(0);

  const handleTabChange = (event, newValue) => {
    setActiveTab(newValue);
  };

  const handleClose = () => {
    setActiveTab(0);
    onClose();
  };

  return (
    <Dialog
      open={open}
      onClose={handleClose}
      maxWidth="sm"
      fullWidth
      PaperProps={{
        sx: {
          minHeight: "60vh",
        },
      }}
    >
      <DialogTitle
        sx={{
          m: 0,
          p: 2,
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
        }}
      >
        <Tabs value={activeTab} onChange={handleTabChange} sx={{ flexGrow: 1 }}>
          <Tab label="Pin Module" />
          <Tab label="WebSocket Module" />
        </Tabs>
        <IconButton
          aria-label="close"
          onClick={handleClose}
          sx={{
            color: (theme) => theme.palette.grey[500],
          }}
        >
          <CloseIcon />
        </IconButton>
      </DialogTitle>
      <DialogContent dividers sx={{ p: 0 }}>
        <Box sx={{ display: activeTab === 0 ? "block" : "none" }}>
          <PinSettingsModal
            open={true}
            onClose={handleClose}
            onSave={onSavePin}
            onDelete={null}
            pinData={pinData}
            onPinDataChange={onPinDataChange}
            hideModalWrapper={true}
          />
        </Box>
        <Box sx={{ display: activeTab === 1 ? "block" : "none" }}>
          <WebSocketSettingsModal
            open={true}
            onClose={handleClose}
            onSave={onSaveWebSocket}
            onDelete={null}
            websocketData={webSocketData}
            onWebSocketDataChange={onWebSocketDataChange}
            hideModalWrapper={true}
          />
        </Box>
      </DialogContent>
    </Dialog>
  );
}
