import React from "react";
import SaveIcon from "@mui/icons-material/Save";
import IButton from "./IButton";

const SaveButton = ({ onClick, tooltip }) => {
  const handleClick = () => {
    console.log("SaveButton clicked");
    if (onClick) {
      onClick();
    }
  };

  return (
    <IButton
      color="primary"
      tooltip={tooltip}
      onClick={handleClick}
      Icon={SaveIcon}
    />
  );
};

export default SaveButton;
