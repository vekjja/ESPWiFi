import React from "react";
import FactCheckIcon from "@mui/icons-material/FactCheck";
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
      Icon={FactCheckIcon}
    />
  );
};

export default SaveButton;
