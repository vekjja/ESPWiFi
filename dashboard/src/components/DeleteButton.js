import React from "react";
import { useTheme } from "@mui/material";
import IButton from "./IButton";
import { getDeleteIcon } from "../utils/themeUtils";

const DeleteButton = ({ onClick, tooltip, sx = {}, disabled = false }) => {
  const theme = useTheme();
  const DeleteIcon = getDeleteIcon(theme);

  return (
    <IButton
      color="error"
      tooltip={tooltip}
      onClick={onClick}
      Icon={DeleteIcon}
      sx={sx}
      disabled={disabled}
      tooltipPlacement="top"
    />
  );
};

export default DeleteButton;
