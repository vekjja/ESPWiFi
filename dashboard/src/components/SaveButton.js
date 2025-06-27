import React from "react";
import SaveIcon from "@mui/icons-material/Save";
import IButton from "./IButton";

const SaveButton = ({ onClick, tooltip }) => {
  return (
    <IButton
      color="primary"
      tooltip={tooltip}
      onClick={onClick}
      Icon={SaveIcon}
    />
  );
};

export default SaveButton;
