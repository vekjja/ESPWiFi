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
      variant="outlined"
      inputProps={{ readOnly: true }}
      sx={{
        "& .MuiInputBase-input": {
          fontFamily:
            "ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace",
          filter: blur && !show ? `blur(${blurPx}px)` : "none",
          transition: "filter 120ms ease-in-out",
        },
      }}
      InputProps={{
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
