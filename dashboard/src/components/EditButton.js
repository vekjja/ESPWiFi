import React from "react";
import IButton from "./IButton";
import EditIcon from "@mui/icons-material/Edit";

const EditButton = ({
  onClick,
  tooltip = "Edit",
  sx = {},
  disabled = false,
  isEditing = false,
}) => {
  return (
    <IButton
      color={isEditing ? "secondary" : "primary"}
      tooltip={tooltip}
      onClick={onClick}
      Icon={EditIcon}
      sx={sx}
      disabled={disabled}
      tooltipPlacement="top"
    />
  );
};

export default EditButton;
