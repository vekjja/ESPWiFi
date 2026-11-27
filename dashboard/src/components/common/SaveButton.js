import React from "react";
import IButton from "./IButton";
import SaveIcon from "@mui/icons-material/Save";

const SaveButton = ({
  onClick,
  tooltip = "Save",
  sx = {},
  disabled = false,
}) => {
  return (
    <IButton
      color="primary"
      tooltip={tooltip}
      onClick={onClick}
      Icon={SaveIcon}
      sx={sx}
      disabled={disabled}
      tooltipPlacement="top"
    />
  );
};

export default SaveButton;
