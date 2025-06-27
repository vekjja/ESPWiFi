import React from "react";
import SettingsIcon from "@mui/icons-material/Settings";
import IButton from "./IButton";

const SettingsButton = ({
  onClick,
  tooltip,
  color,
  sx = { position: "absolute", left: 0, bottom: 0, m: 2 },
}) => {
  return (
    <IButton
      color={color}
      tooltip={tooltip}
      onClick={onClick}
      Icon={SettingsIcon}
      sx={sx}
    />
  );
};

export default SettingsButton;
