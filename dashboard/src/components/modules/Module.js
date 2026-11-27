import React, { useEffect, useRef, useState } from "react";
import IButton from "../common/IButton";
import DeleteButton from "../common/DeleteButton";
import { Box, Card, CardContent, Typography } from "@mui/material";
import RestartAltIcon from "@mui/icons-material/RestartAlt";
import SettingsIcon from "@mui/icons-material/Settings";
import PhotoSizeSelectSmallIcon from "@mui/icons-material/PhotoSizeSelectSmall";
import VrpanoIcon from "@mui/icons-material/Vrpano";
import { chromeForModule, frameSxForModule } from "./moduleConfig";
import SectionTitle from "../common/SectionTitle";

const MIN_MODULE_WIDTH = 220;
const MIN_MODULE_HEIGHT = 160;
const SCALE_BASIS = 340;

function moduleScale(width, height) {
  const w = Number(width);
  const h = Number(height);
  if (!w || !h) return 1;
  const basis = Math.min(w, h) / SCALE_BASIS;
  return Math.round(Math.max(0.7, Math.min(2.4, basis)) * 100) / 100;
}

export default function Module({
  title,
  icon,
  children,
  onSettings,
  onDelete,
  onReconnect,
  settingsTooltip = "Settings",
  deleteTooltip = "Delete",
  reconnectTooltip = "Reconnect",
  reconnectIcon = RestartAltIcon,
  reconnectColor = "secondary",
  sx = {},
  bottomContent, // Add new prop
  settingsDisabled = false, // Add new prop for disabling settings
  errorOutline = false, // Add new prop for error outline
  onResize,
  onToggleWide,
  moduleConfig,
  connectionStatus,
  online,
  active,
  playback,
}) {
  const cardRef = useRef(null);
  const resizeRef = useRef(null);
  const [liveSize, setLiveSize] = useState(null);
  const [touched, setTouched] = useState(false);
  const [touchTick, setTouchTick] = useState(0);
  const wide = !!moduleConfig?.wide;
  const frame = moduleConfig ? frameSxForModule(moduleConfig) : null;
  const savedScale =
    moduleConfig?.size === "custom"
      ? moduleScale(moduleConfig.width, moduleConfig.height)
      : 1;
  const scale = liveSize
    ? moduleScale(liveSize.width, liveSize.height)
    : savedScale;
  const chrome = chromeForModule({
    connectionStatus,
    online,
    active,
    playback,
  });

  const startResize = (event) => {
    event.stopPropagation();
    event.preventDefault();
    const rect = cardRef.current?.getBoundingClientRect();
    if (!rect) return;
    resizeRef.current = {
      x: event.clientX,
      y: event.clientY,
      width: rect.width,
      height: rect.height,
    };
    event.currentTarget.setPointerCapture(event.pointerId);
  };

  const moveResize = (event) => {
    const drag = resizeRef.current;
    if (!drag) return;
    const width = Math.max(
      MIN_MODULE_WIDTH,
      Math.round(drag.width + event.clientX - drag.x)
    );
    const height = Math.max(
      MIN_MODULE_HEIGHT,
      Math.round(drag.height + event.clientY - drag.y)
    );
    drag.size = { width, height };
    setLiveSize(drag.size);
  };

  const endResize = (event) => {
    const drag = resizeRef.current;
    resizeRef.current = null;
    if (event.currentTarget.hasPointerCapture?.(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
    if (drag?.size && onResize) onResize(drag.size);
    setLiveSize(null);
  };

  useEffect(() => {
    if (!touched) return undefined;
    const hide = setTimeout(() => setTouched(false), 6000);
    const close = (event) => {
      if (!cardRef.current?.contains(event.target)) setTouched(false);
    };
    document.addEventListener("pointerdown", close);
    return () => {
      clearTimeout(hide);
      document.removeEventListener("pointerdown", close);
    };
  }, [touched, touchTick]);

  const revealOnTouch = () => {
    if (window.matchMedia("(hover: hover) and (pointer: fine)").matches) return;
    setTouched(true);
    setTouchTick((tick) => tick + 1);
  };

  const liveFrame = liveSize
    ? {
        height: liveSize.height,
        minHeight: liveSize.height,
        maxHeight: liveSize.height,
        ...(wide
          ? null
          : {
              width: liveSize.width,
              minWidth: liveSize.width,
              maxWidth: liveSize.width,
            }),
      }
    : null;

  return (
    <Card
      ref={cardRef}
      className={touched ? "module-touched" : undefined}
      onClick={revealOnTouch}
      sx={{
        padding: "10px",
        margin: "10px",
        border: "1px solid",
        borderColor: "primary.main",
        borderRadius: "5px",
        backgroundColor: "secondary.light",
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        position: "relative",
        cursor: "grab",
        maxWidth: "100%",
        boxSizing: "border-box",
        overflow: "hidden",
        "--module-scale": String(scale),
        "&:active": {
          cursor: "grabbing",
        },
        ...frame,
        ...chrome,
        ...(errorOutline ? { borderColor: "error.main" } : null),
        ...sx,
        ...liveFrame,
        "& .module-actions, & .module-actions *": {
          opacity: liveSize ? 1 : 0,
          pointerEvents: "none",
          transition: "opacity 500ms ease",
        },
        "@media (hover: hover) and (pointer: fine)": {
          "&:hover .module-actions, &:hover .module-actions *": {
            opacity: 1,
            pointerEvents: "auto",
          },
        },
        "@media (hover: none), (pointer: coarse)": {
          "&.module-touched .module-actions, &.module-touched .module-actions *": {
            opacity: 1,
            pointerEvents: "auto",
          },
        },
      }}
    >
      <CardContent
        sx={{
          width: "100%",
          maxWidth: "100%",
          flex: "1 1 auto",
          padding: 0,
          "&:last-child": { paddingBottom: 0 },
          display: "flex",
          flexDirection: "column",
          alignItems: "stretch",
          justifyContent: "flex-start",
          minHeight: 0,
          overflow: "hidden",
          boxSizing: "border-box",
          "& > .MuiCard-root": {
            flex: "1 1 auto",
            minHeight: 0,
            width: "100%",
          },
        }}
      >
        {icon ? (
          <SectionTitle icon={icon} title={title} sx={{ mb: 1 }} />
        ) : title ? (
          <Typography variant="body1" align="center" component="div">
            {title}
          </Typography>
        ) : null}
        {children}
      </CardContent>
      {/* Stack bottomContent and action buttons in a flex column at the bottom */}
      <div
        style={{
          width: "100%",
          display: "flex",
          flexDirection: "column",
          gap: 4,
          marginTop: "auto",
        }}
      >
        {bottomContent && (
          <div style={{ width: "100%", zIndex: 1 }}>{bottomContent}</div>
        )}
        <div
          className="module-actions"
          style={{
            width: "100%",
            minHeight: 36,
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            flex: "0 0 auto",
          }}
        >
          <div style={{ display: "flex", alignItems: "center" }}>
            {onDelete && (
              <DeleteButton
                onClick={onDelete}
                tooltip={deleteTooltip}
              />
            )}
            {onSettings && (
              <IButton
                color="default"
                onClick={onSettings}
                tooltip={settingsTooltip}
                tooltipPlacement="bottom"
                Icon={SettingsIcon}
                disabled={settingsDisabled}
              />
            )}
          </div>
          <div
            style={{
              display: "flex",
              alignItems: "center",
            }}
          >
            {onReconnect && (
              <IButton
                onClick={(e) => {
                  e.stopPropagation();
                  onReconnect();
                }}
                tooltip={reconnectTooltip}
                Icon={reconnectIcon}
                color={reconnectColor}
                tooltipPlacement="bottom"
                sx={{ mr: 0.5 }}
              />
            )}
            {onToggleWide && (
              <Box
                data-no-dnd="true"
                onClick={(event) => {
                  event.stopPropagation();
                  onToggleWide();
                }}
                sx={{
                  display: "flex",
                  cursor: "pointer",
                  color: wide ? "primary.main" : "text.secondary",
                  mr: 0.5,
                }}
              >
                <VrpanoIcon sx={{ fontSize: 22 }} />
              </Box>
            )}
            {onResize && (
              <Box
                data-no-dnd="true"
                onPointerDown={startResize}
                onPointerMove={moveResize}
                onPointerUp={endResize}
                onPointerCancel={endResize}
                sx={{
                  display: "flex",
                  cursor: "nwse-resize",
                  color: "primary.main",
                  touchAction: "none",
                }}
              >
                <PhotoSizeSelectSmallIcon sx={{ fontSize: 20 }} />
              </Box>
            )}
          </div>
        </div>
      </div>
    </Card>
  );
}
