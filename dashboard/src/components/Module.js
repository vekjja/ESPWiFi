import React from "react";
import IButton from "./IButton";
import DeleteButton from "./DeleteButton";
import SettingsButton from "./SettingsButton";
import { RestartAlt } from "@mui/icons-material";
import { Card, CardContent, Typography } from "@mui/material";

export default function Module({
  title,
  children,
  onSettings,
  onDelete,
  onReconnect,
  settingsTooltip = "Settings",
  deleteTooltip = "Delete",
  reconnectTooltip = "Reconnect",
  reconnectIcon = RestartAlt,
  reconnectColor = "secondary",
  sx = {},
}) {
  return (
    <Card
      sx={{
        padding: "10px",
        margin: "10px",
        minWidth: "200px",
        minHeight: "200px",
        maxWidth: "200px",
        maxHeight: "200px",
        border: "1px solid",
        borderColor: "primary.main",
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
        ...sx,
      }}
    >
      <CardContent
        sx={{
          width: "100%",
          flex: "1 1 auto",
          overflowY: "auto",
          padding: 0,
          paddingBottom: 0,
          "&:last-child": { paddingBottom: 0 },
        }}
      >
        <Typography variant="body1" align="center">
          {title}
        </Typography>
        {children}
      </CardContent>
      <div
        style={{
          width: "100%",
          display: "flex",
          justifyContent: "space-between",
          alignItems: "center",
          position: "absolute",
          bottom: 8,
          left: 0,
          zIndex: 2,
          pointerEvents: "none",
        }}
      >
        <div style={{ pointerEvents: "auto" }}>
          {onSettings && (
            <SettingsButton
              color="default"
              onClick={onSettings}
              tooltip={settingsTooltip}
              tooltipPlacement="bottom"
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
    </Card>
  );
}
