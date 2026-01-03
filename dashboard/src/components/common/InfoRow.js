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
export default function InfoRow({ label, value, sx = {} }) {
  return (
    <Box
      sx={{
        display: "flex",
        flexDirection: "column",
        alignItems: "flex-start",
        gap: 0.5,
        ...sx,
      }}
    >
      <Typography 
        variant="caption" 
        color="text.secondary"
        sx={{ 
          textTransform: "uppercase", 
          fontSize: "0.65rem",
          fontWeight: 600,
          letterSpacing: 0.5
        }}
      >
        {label}
      </Typography>
      {typeof value === "string" || typeof value === "number" ? (
        <Typography 
          variant="h6" 
          sx={{ 
            fontWeight: 600,
            fontFamily: "monospace",
            fontSize: "1.1rem"
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
