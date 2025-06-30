import React from "react";
import SettingsIcon from "@mui/icons-material/Settings";
import IButton from "./IButton";

/**
 * SettingsButton - A settings icon button with tooltip and passthrough props.
 * Props:
 *   - All IButton props are supported.
 *   - Defaults: color="default", tooltipPlacement="top"
 */
const SettingsButton = ({
  onClick,
  tooltip = "Settings",
  color = "default",
  sx = {},
  tooltipPlacement = "top",
  ...rest
}) => {
  return (
    <IButton
      color={color}
      tooltip={tooltip}
      onClick={onClick}
      Icon={SettingsIcon}
      sx={sx}
      tooltipPlacement={tooltipPlacement}
      {...rest}
    />
  );
};

export default SettingsButton;
