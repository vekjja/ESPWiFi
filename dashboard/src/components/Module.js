import React from "react";
import { Card, CardContent, Typography, Box } from "@mui/material";
import SettingsButton from "./SettingsButton";
import DeleteButton from "./DeleteButton";

export default function Module({
  title,
  children,
  onSettings,
  onDelete,
  settingsTooltip = "Settings",
  deleteTooltip = "Delete",
  sx = {},
}) {
  return (
    <Card
      sx={{
        padding: "10px",
        margin: "10px",
        // maxWidth: "200px",
        minWidth: "200px",
        border: "1px solid",
        borderColor: "primary.main",
        borderRadius: "5px",
        backgroundColor: "secondary.light",
        display: "flex",
        flexDirection: "column",
        alignItems: "center",
        position: "relative",
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
      <Box
        sx={{
          position: "absolute",
          left: 0,
          bottom: 0,
          m: 1,
          display: "flex",
          gap: 1,
        }}
      >
        {onSettings && (
          <SettingsButton
            color="secondary"
            onClick={onSettings}
            tooltip={settingsTooltip}
          />
        )}
        {onDelete && (
          <DeleteButton onClick={onDelete} tooltip={deleteTooltip} />
        )}
      </Box>
    </Card>
  );
}
