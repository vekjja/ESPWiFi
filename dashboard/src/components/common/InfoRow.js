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
        justifyContent: "space-between",
        alignItems: "center",
        ...sx,
      }}
    >
      <Typography variant="body2" color="text.secondary">
        {label}
      </Typography>
      {typeof value === "string" || typeof value === "number" ? (
        <Typography variant="body1" sx={{ fontWeight: 500 }}>
          {value}
        </Typography>
      ) : (
        value
      )}
    </Box>
  );
}
