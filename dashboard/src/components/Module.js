import React from "react";
import IButton from "./IButton";
import DeleteButton from "./DeleteButton";
import { Card, CardContent, Typography } from "@mui/material";
import RestartAltIcon from "@mui/icons-material/RestartAlt";
import SettingsIcon from "@mui/icons-material/Settings";

// Height in px for bottom controls (input/send + buttons)
const BOTTOM_CONTROLS_HEIGHT = 88;

export default function Module({
  title,
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
}) {
  return (
    <Card
      sx={{
        padding: "10px",
        margin: "10px",
        // minWidth: "200px",
        // maxWidth: "400px",
        // Removed minHeight and maxHeight to allow natural height expansion
        border: "1px solid",
        borderColor: errorOutline ? "error.main" : "primary.main",
        borderRadius: "5px",
        backgroundColor: "secondary.light",
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        position: "relative",
        cursor: "grab",
        "&:active": {
          cursor: "grabbing",
        },
        ...sx, // Sizing is now controlled by sx from parent modules
      }}
    >
      <CardContent
        sx={{
          width: "100%",
          flex: "1 1 auto",
          padding: 0,
          paddingBottom: `${BOTTOM_CONTROLS_HEIGHT}px`,
          "&:last-child": { paddingBottom: 0 },
          display: "flex",
          flexDirection: "column",
          alignItems: "center",
          justifyContent: "flex-start",
          minHeight: "120px", // Ensure minimum height for all modules
        }}
      >
        <Typography variant="body1" align="center">
          {title}
        </Typography>
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
          style={{
            width: "100%",
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            // Remove absolute positioning
            // position: "absolute",
            // bottom: 8,
            // left: 0,
            zIndex: 2,
            pointerEvents: "none",
            paddingBottom: 8,
            paddingTop: 2,
          }}
        >
          <div style={{ pointerEvents: "auto" }}>
            {onSettings && (
              <IButton
                color="default"
                onClick={onSettings}
                tooltip={settingsTooltip}
                tooltipPlacement="bottom"
                Icon={SettingsIcon}
                disabled={settingsDisabled}
                sx={{ pointerEvents: "auto" }}
              />
            )}
          </div>
          <div
            style={{
              display: "flex",
              alignItems: "center",
              pointerEvents: "auto",
            }}
          >
            {onDelete && (
              <DeleteButton
                onClick={onDelete}
                tooltip={deleteTooltip}
                sx={{
                  marginLeft: 8,
                  marginRight: 8,
                  pointerEvents: "auto",
                }}
              />
            )}
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
                sx={{ pointerEvents: "auto", mr: 1 }}
              />
            )}
          </div>
        </div>
      </div>
    </Card>
  );
}
