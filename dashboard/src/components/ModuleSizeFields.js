import React from "react";
import { Box, FormControl, InputLabel, MenuItem, Select, TextField } from "@mui/material";
import { MODULE_SIZES, MODULE_SIZE_OPTIONS } from "./moduleConfig";

/**
 * Size editor shared by module settings. Values are the module config fields
 * `size`, `width`, and `height`.
 */
export default function ModuleSizeFields({
  size = "medium",
  width = MODULE_SIZES.medium.width,
  height = MODULE_SIZES.medium.height,
  onChange,
}) {
  return (
    <>
      <FormControl fullWidth sx={{ marginBottom: 2 }}>
        <InputLabel id="module-size-select-label">Size</InputLabel>
        <Select
          labelId="module-size-select-label"
          value={size}
          label="Size"
          onChange={(e) => onChange({ size: e.target.value })}
          data-no-dnd="true"
        >
          {MODULE_SIZE_OPTIONS.map((option) => (
            <MenuItem key={option.value} value={option.value}>
              {option.label}
            </MenuItem>
          ))}
        </Select>
      </FormControl>
      {size === "custom" && (
        <Box sx={{ display: "flex", gap: 2, marginBottom: 2 }}>
          <TextField
            label="Width (px)"
            type="number"
            value={width}
            onChange={(e) => onChange({ width: Number(e.target.value) })}
            variant="outlined"
            sx={{ flex: 1 }}
            data-no-dnd="true"
          />
          <TextField
            label="Height (px)"
            type="number"
            value={height}
            onChange={(e) => onChange({ height: Number(e.target.value) })}
            variant="outlined"
            sx={{ flex: 1 }}
            data-no-dnd="true"
          />
        </Box>
      )}
    </>
  );
}
