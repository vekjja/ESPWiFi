/**
 * @file MaskedValueField.js
 * @brief Read-only field that can blur/hide sensitive strings with an eye toggle.
 */

import React, { useState } from "react";
import { IconButton, TextField } from "@mui/material";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";

export default function MaskedValueField({
  value,
  blur = true,
  defaultShow = false,
  blurPx = 6,
}) {
  const [show, setShow] = useState(Boolean(defaultShow));
  const canToggle = Boolean(blur);

  return (
    <TextField
      fullWidth
      size="small"
      value={value || ""}
      variant="standard"
      multiline
      minRows={1}
      maxRows={3}
      inputProps={{ readOnly: true }}
      sx={{
        // Remove underline/border for a cleaner "read-only" look.
        "& .MuiInput-underline:before": { borderBottom: "none" },
        "& .MuiInput-underline:hover:not(.Mui-disabled):before": {
          borderBottom: "none",
        },
        "& .MuiInput-underline:after": { borderBottom: "none" },
        "& .MuiInputBase-input": {
          fontFamily:
            "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace",
          filter: blur && !show ? `blur(${blurPx}px)` : "none",
          transition: "filter 120ms ease-in-out",
          // Keep alignment with InfoRow value text.
          paddingTop: "2px",
          paddingBottom: "2px",
          lineHeight: 1.35,
          wordBreak: "break-all",
        },
      }}
      InputProps={{
        disableUnderline: true,
        endAdornment: canToggle ? (
          <IconButton
            size="small"
            onClick={() => setShow((v) => !v)}
            aria-label={show ? "Hide value" : "Show value"}
            edge="end"
          >
            {show ? <VisibilityOffIcon /> : <VisibilityIcon />}
          </IconButton>
        ) : null,
      }}
    />
  );
}
