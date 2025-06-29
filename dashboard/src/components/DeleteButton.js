import React from "react";
import IButton from "./IButton";
import DeleteIcon from "@mui/icons-material/Delete";

const DeleteButton = ({ onClick, tooltip, sx = {} }) => {
  return (
    <IButton
      color="error"
      tooltip={tooltip}
      onClick={onClick}
      Icon={DeleteIcon}
      sx={sx}
      tooltipPlacement="top"
    />
  );
};

export default DeleteButton;
