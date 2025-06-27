import React from "react";
import IconButton from "@mui/material/IconButton";
import Tooltip from "@mui/material/Tooltip";

const IButton = ({ onClick, tooltip, Icon, color, disabled = false }) => {
  return (
    <Tooltip title={tooltip}>
      <span>
        <IconButton
          color={color}
          onClick={onClick}
          sx={{ mt: 2, ml: 2 }}
          disabled={disabled}
        >
          <Icon />
        </IconButton>
      </span>
    </Tooltip>
  );
};

export default IButton;
