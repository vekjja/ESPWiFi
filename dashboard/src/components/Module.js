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
          padding: 0,
          paddingBottom: "48px",
          "&:last-child": { paddingBottom: 0 },
        }}
      >
        <Typography variant="body1" align="center">
          {title}
        </Typography>
        {children}
      </CardContent>
      {onSettings && (
        <SettingsButton
          color="default"
          onClick={onSettings}
          tooltip={settingsTooltip}
        />
      )}
      {onDelete && (
        <DeleteButton
          onClick={onDelete}
          tooltip={deleteTooltip}
          sx={{
            position: "absolute",
            left: 48,
            bottom: 8,
          }}
        />
      )}
      {onReconnect && (
        <IButton
          onClick={(e) => {
            e.stopPropagation(); // Prevent drag activation
            onReconnect();
          }}
          tooltip={reconnectTooltip}
          Icon={reconnectIcon}
          color={reconnectColor}
          enhanced={true}
          tooltipPlacement="top"
          sx={{
            position: "absolute",
            right: 8,
            bottom: 8,
            zIndex: 1,
          }}
        />
      )}
    </Card>
  );
}
