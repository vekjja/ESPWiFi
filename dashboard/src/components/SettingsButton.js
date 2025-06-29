import React from "react";
import SettingsIcon from "@mui/icons-material/Settings";
import IButton from "./IButton";

const SettingsButton = ({ onClick, tooltip, color = "default", sx = {} }) => {
  return (
    <IButton
      color={color}
      tooltip={tooltip}
      onClick={onClick}
      Icon={SettingsIcon}
      sx={{
        position: "absolute",
        left: 8,
        bottom: 8,
        ...sx,
      }}
      tooltipPlacement="top"
    />
  );
};

export default SettingsButton;
