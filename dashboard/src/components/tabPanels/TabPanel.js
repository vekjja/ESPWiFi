import React from "react";
import { Box } from "@mui/material";

export default function TabPanel({ children, value, index, ...other }) {
  return (
    <Box
      role="tabpanel"
      id={`tabpanel-${index}`}
      aria-labelledby={`tab-${index}`}
      sx={{
        flex: 1,
        minHeight: 0,
        display: value === index ? "flex" : "none",
        flexDirection: "column",
      }}
      {...other}
    >
      <Box
        sx={{
          pt: 2,
          flex: 1,
          minHeight: 0,
          display: "flex",
          flexDirection: "column",
        }}
      >
        {children}
      </Box>
    </Box>
  );
}
