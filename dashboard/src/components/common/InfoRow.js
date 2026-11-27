import React from "react";
import { Box, Typography } from "@mui/material";

/**
 * Simple component for displaying label-value pairs in info cards
 * Provides consistent styling across the application
 *
 * @param {Object} props - Component props
 * @param {string} props.label - Left-side label
 * @param {React.ReactNode} props.value - Right-side value (can be string, number, or component)
 * @param {Object} props.sx - Additional sx styling
 */
export default function InfoRow({
  label,
  value,
  inline = false,
  valueSize = "calc(1.35rem * var(--module-scale, 1))",
  sx = {},
}) {
  return (
    <Box
      sx={{
        display: "flex",
        flexDirection: inline ? "row" : "column",
        alignItems: inline ? "baseline" : "flex-start",
        justifyContent: inline ? "space-between" : undefined,
        gap: "calc(4px * var(--module-scale, 1))",
        minWidth: 0,
        ...sx,
      }}
    >
      <Typography 
        variant="caption" 
        color="text.secondary"
        sx={{ 
          textTransform: "uppercase", 
          fontSize: "calc(0.8rem * var(--module-scale, 1))",
          lineHeight: 1.2,
          fontWeight: 600,
          letterSpacing: 0.5
        }}
      >
        {label}
      </Typography>
      {typeof value === "string" || typeof value === "number" ? (
        <Typography
          component="div"
          sx={{
            fontWeight: 600,
            fontFamily: "monospace",
            fontSize: valueSize,
            lineHeight: 1.2,
            margin: 0,
          }}
        >
          {value}
        </Typography>
      ) : (
        value
      )}
    </Box>
  );
}
