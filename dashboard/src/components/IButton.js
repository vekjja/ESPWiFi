import React from "react";
import IconButton from "@mui/material/IconButton";
import Tooltip from "@mui/material/Tooltip";

const IButton = ({
  onClick,
  tooltip,
  Icon,
  color = "default",
  sx = {},
  disabled = false,
  enhanced = false,
  tooltipPlacement = "top",
}) => {
  // Determine tooltip placement based on button position
  const getTooltipPlacement = () => {
    if (tooltipPlacement !== "top") return tooltipPlacement;

    // Auto-detect placement based on sx positioning
    const sxStr = JSON.stringify(sx);
    if (sxStr.includes("bottom") && sxStr.includes("left")) {
      return "top";
    } else if (sxStr.includes("bottom") && sxStr.includes("right")) {
      return "top";
    } else if (sxStr.includes("top")) {
      return "bottom";
    }
    return "top";
  };

  const baseSx = {
    transition: "all 0.2s ease-in-out",
    "&:hover": {
      transform: enhanced ? "scale(1.05)" : "scale(1.02)",
    },
    ...sx,
  };

  const enhancedSx = enhanced
    ? {
        ...baseSx,
        backgroundColor:
          color === "success"
            ? "success.main"
            : color === "error"
            ? "error.main"
            : color === "primary"
            ? "primary.main"
            : "secondary.main",
        color: "white",
        boxShadow: 1,
        "&:hover": {
          ...baseSx["&:hover"],
          backgroundColor:
            color === "success"
              ? "success.dark"
              : color === "error"
              ? "error.dark"
              : color === "primary"
              ? "primary.dark"
              : "secondary.dark",
          boxShadow: 3,
        },
        "&:active": {
          transform: "scale(0.95)",
        },
      }
    : baseSx;

  return (
    <Tooltip
      title={tooltip}
      placement={getTooltipPlacement()}
      arrow
      enterDelay={300}
      leaveDelay={0}
      enterNextDelay={150}
      componentsProps={{
        tooltip: {
          sx: {
            fontSize: "0.75rem",
            backgroundColor: "rgba(0, 0, 0, 0.87)",
            color: "white",
            padding: "4px 8px",
            pointerEvents: "none",
          },
        },
      }}
    >
      <span>
        <IconButton
          color={color}
          onClick={onClick}
          sx={enhancedSx}
          disabled={disabled}
          data-no-dnd="true"
          size="small"
        >
          <Icon />
        </IconButton>
      </span>
    </Tooltip>
  );
};

export default IButton;
