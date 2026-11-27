import React from "react";
import { Box, Typography } from "@mui/material";
import MemoryIcon from "@mui/icons-material/Memory";
import LeakAddIcon from "@mui/icons-material/LeakAdd";
import StorageIcon from "@mui/icons-material/Storage";
import SdCardIcon from "@mui/icons-material/SdCard";
import CameraAltIcon from "@mui/icons-material/CameraAlt";

/** Names and icons shared by settings sections and dashboard modules. */
export const SECTIONS = {
  memory: { title: "Memory", icon: MemoryIcon },
  network: { title: "Network", icon: LeakAddIcon },
  storage: { title: "Storage", icon: StorageIcon },
  littlefs: { title: "LittleFS", icon: StorageIcon },
  sd: { title: "SD Card", icon: SdCardIcon },
  camera: { title: "Camera", icon: CameraAltIcon },
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
      sx={{
        display: "flex",
        alignItems: "center",
        gap: 1,
        width: "100%",
        minWidth: 0,
        ...sx,
      }}
    >
      {Icon ? <Icon color={iconColor} fontSize={iconSize} /> : null}
      <Typography variant={variant} component="div">
        {title}
      </Typography>
      {children}
    </Box>
  );
}
