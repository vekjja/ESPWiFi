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
    <Tooltip title={tooltip} placement="bottom">
      {disabled ? (
        <span>
          <IconButton
            color={color}
            onClick={onClick}
            sx={sx}
            disabled={disabled}
          >
            <Icon />
          </IconButton>
        </span>
      ) : (
        <IconButton color={color} onClick={onClick} sx={sx} disabled={disabled}>
          <Icon />
        </IconButton>
      )}
    </Tooltip>
  );
};

export default IButton;
