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
      enterDelay={500}
      leaveDelay={0}
      enterNextDelay={300}
      componentsProps={{
        tooltip: {
          sx: {
            mt: 1, // Add margin top to push tooltip down
            pointerEvents: "none", // Prevent tooltip from blocking clicks
          },
        },
      }}
    >
      <span>
        <IconButton
          color={color}
          onClick={onClick}
          sx={sx}
          disabled={disabled}
          data-no-dnd="true"
        >
          <Icon />
        </IconButton>
      </span>
    </Tooltip>
  );
};

export default IButton;
