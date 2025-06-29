import React from "react";
import IconButton from "@mui/material/IconButton";
import Tooltip from "@mui/material/Tooltip";

const IButton = ({
  onClick,
  tooltip,
  Icon,
  color,
  sx = { mt: 2, ml: 2 },
  disabled = false,
}) => {
  return (
    <Tooltip
      title={tooltip}
      placement="bottom"
      arrow
      componentsProps={{
        tooltip: {
          sx: {
            mt: 1, // Add margin top to push tooltip down
          },
        },
      }}
    >
      <span>
        <IconButton color={color} onClick={onClick} sx={sx} disabled={disabled}>
          <Icon />
        </IconButton>
      </span>
    </Tooltip>
  );
};

export default IButton;
