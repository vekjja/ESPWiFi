import React from "react";
import IconButton from "@mui/material/IconButton";
import Tooltip from "@mui/material/Tooltip";

/**
 * IButton - A reusable icon button with optional tooltip.
 * Props:
 *   - onClick: function
 *   - tooltip: string (optional)
 *   - Icon: React component (icon)
 *   - color: MUI color (default: "default")
 *   - sx: style overrides
 *   - disabled: boolean
 *   - tooltipPlacement: string (default: "bottom")
 *   - className, id, ...rest: passthrough
 */
const IButton = ({
  onClick,
  tooltip,
  Icon,
  color = "default",
  sx = {},
  disabled = false,
  tooltipPlacement = "bottom",
  className,
  id,
  ...rest
}) => {
  const button = (
    <IconButton
      color={color}
      onClick={onClick}
      sx={sx}
      disabled={disabled}
      data-no-dnd="true"
      size="small"
      className={className}
      id={id}
      {...rest}
    >
      {Icon && <Icon />}
    </IconButton>
  );
  return tooltip ? (
    <Tooltip
      title={tooltip}
      placement={tooltipPlacement}
      slotProps={{ popper: { disablePortal: true } }}
    >
      <span role="presentation">{button}</span>
    </Tooltip>
  ) : (
    button
  );
};

export default IButton;
