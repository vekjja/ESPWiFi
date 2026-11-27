import React from "react";
import { Box, Typography } from "@mui/material";
import MemoryIcon from "@mui/icons-material/Memory";
import LeakAddIcon from "@mui/icons-material/LeakAdd";
import StorageIcon from "@mui/icons-material/Storage";
import SdCardIcon from "@mui/icons-material/SdCard";
import CameraAltIcon from "@mui/icons-material/CameraAlt";
import InputIcon from "@mui/icons-material/Input";
import WifiIcon from "@mui/icons-material/Wifi";
import LibraryMusicIcon from "@mui/icons-material/LibraryMusic";

/** Names and icons shared by settings sections and dashboard modules. */
export const SECTIONS = {
  memory: { title: "Memory", icon: MemoryIcon },
  network: { title: "Network", icon: LeakAddIcon },
  storage: { title: "Storage", icon: StorageIcon },
  littlefs: { title: "LittleFS", icon: StorageIcon },
  sd: { title: "SD Card", icon: SdCardIcon },
  camera: { title: "Camera", icon: CameraAltIcon },
  pin: { title: "Pin", icon: InputIcon },
  websocket: { title: "WebSocket", icon: WifiIcon },
  music: { title: "Music Player", icon: LibraryMusicIcon },
};

/**
 * Title row used by settings cards and dashboard modules.
 * Main titles use the primary icon color; nested labels pass iconColor="inherit".
 */
export default function SectionTitle({
  icon: Icon,
  title,
  variant = "h6",
  iconColor = "primary",
  iconSize,
  sx,
  children,
}) {
  return (
    <Box
      className="section-title"
      sx={{
        display: "flex",
        alignItems: "center",
        gap: 1,
        width: "100%",
        minWidth: 0,
        position: "relative",
        ...sx,
      }}
    >
      {Icon ? <Icon color={iconColor} fontSize={iconSize} /> : null}
      <Typography
        variant={variant}
        component="div"
        noWrap
        sx={{ minWidth: 0 }}
      >
        {title}
      </Typography>
      {children ? (
        <Box sx={{ ml: "auto", display: "flex", alignItems: "center", minWidth: 0 }}>
          {children}
        </Box>
      ) : null}
    </Box>
  );
}
