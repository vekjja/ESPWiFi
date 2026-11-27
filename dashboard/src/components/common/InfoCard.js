/**
 * @file InfoCard.js
 * @brief Reusable card component for device information display
 *
 * Provides a consistent card layout with optional edit functionality,
 * icon, and collapsible content sections.
 */

import React from "react";
import {
  Box,
  Card,
  CardContent,
  IconButton,
  Tooltip,
  Collapse,
  Grid,
} from "@mui/material";
import EditIcon from "@mui/icons-material/Edit";
import SectionTitle from "./SectionTitle";

/** Card surface shared by settings sections and dashboard modules. */
export const infoCardSurfaceSx = {
  backgroundColor: "background.paper",
  backgroundImage:
    "linear-gradient(rgba(255, 255, 255, 0.05), rgba(255, 255, 255, 0.05))",
  border: "none",
  borderRadius: "3px",
  boxShadow: 1,
};

/** Type, icons, and bars follow --module-scale from the module shell. */
const scaleSx = {
  "&& .MuiTypography-caption": {
    fontSize: "calc(0.8rem * var(--module-scale, 1))",
    lineHeight: 1.2,
  },
  "&& .MuiTypography-h6": {
    fontSize: "calc(1.35rem * var(--module-scale, 1))",
    lineHeight: 1.2,
  },
  "&& .MuiTypography-subtitle2": {
    fontSize: "calc(0.9rem * var(--module-scale, 1))",
    lineHeight: 1.15,
  },
  "&& .MuiTypography-body2": {
    fontSize: "calc(0.875rem * var(--module-scale, 1))",
    lineHeight: 1.15,
  },
  "&& .section-title .MuiTypography-root": {
    fontSize: "calc(1.25rem * var(--module-scale, 1))",
  },
  "&& .section-title .MuiTypography-subtitle2": {
    fontSize: "calc(0.9rem * var(--module-scale, 1))",
  },
  "&& .section-title .MuiSvgIcon-root": {
    fontSize: "calc(24px * var(--module-scale, 1))",
  },
  "&& .MuiLinearProgress-root": {
    height: "calc(8px * var(--module-scale, 1))",
  },
};

/**
 * InfoCard Component
 *
 * @param {Object} props - Component props
 * @param {string} props.title - Card title
 * @param {React.Component} props.icon - Icon component to display
 * @param {boolean} props.editable - Whether the card has edit functionality
 * @param {boolean} props.isEditing - Current edit state
 * @param {Function} props.onEdit - Callback when edit button is clicked
 * @param {React.ReactNode} props.children - Card content
 * @param {React.ReactNode} props.editContent - Content to show in edit mode
 * @param {number|object} props.gridSize - Grid size (xs, sm, md, lg, xl)
 * @param {number} props.minHeight - Minimum card height
 * @returns {JSX.Element} The rendered card
 */
export default function InfoCard({
  title,
  icon: Icon,
  headerActions = null,
  editable = false,
  isEditing = false,
  onEdit,
  children,
  editContent,
  gridSize = { xs: 12 },
  minHeight = 200,
  embedded = false,
  fluid = false,
  scale = false,
  fit = false,
  hug = false,
}) {
  const card = (
      <Card
        sx={{
          height: fluid || !embedded ? "100%" : "auto",
          width: "100%",
          maxWidth: "100%",
          boxSizing: "border-box",
          minWidth: 0,
          minHeight: fluid ? 0 : minHeight,
          flex: fluid ? "1 1 auto" : undefined,
          display: fluid ? "flex" : undefined,
          flexDirection: fluid ? "column" : undefined,
          ...infoCardSurfaceSx,
          ...(scale ? scaleSx : null),
        }}
      >
        <CardContent
          sx={
            fluid
              ? {
                  flex: "1 1 auto",
                  display: "flex",
                  flexDirection: "column",
                  minHeight: 0,
                  width: "100%",
                  boxSizing: "border-box",
                  ...(scale || fit
                    ? {
                        padding: "calc(12px * var(--module-scale, 1))",
                        "&:last-child": {
                          paddingBottom: "calc(12px * var(--module-scale, 1))",
                        },
                      }
                    : null),
                }
              : undefined
          }
        >
          <Box
            sx={{
              display: "flex",
              justifyContent: "space-between",
              alignItems: "center",
              mb: fluid ? 1.5 : 2,
              gap: 1,
              minWidth: 0,
            }}
          >
            <SectionTitle icon={Icon} title={title} sx={{ width: "auto", flex: 1 }}>
              {headerActions}
            </SectionTitle>
            {editable && !isEditing && (
              <Tooltip title={`Edit ${title.toLowerCase()}`}>
                <IconButton onClick={onEdit} size="small">
                  <EditIcon />
                </IconButton>
              </Tooltip>
            )}
          </Box>

          {/* View Mode */}
          {fluid ? (
            <Box
              data-module-fit={fit ? "" : undefined}
              data-hug={hug ? "" : undefined}
              sx={{
                display: "flex",
                flexDirection: "column",
                flex: "1 1 auto",
                minHeight: 0,
                minWidth: 0,
                maxWidth: "100%",
                overflow: fit ? "auto" : "hidden",
              }}
            >
              {children}
            </Box>
          ) : (
          <Collapse in={!isEditing}>
            <Box
              sx={{
                display: "flex",
                flexDirection: "column",
                gap: 1.5,
                minWidth: 0,
                maxWidth: "100%",
              }}
            >
              {children}
            </Box>
          </Collapse>
          )}

          {/* Edit Mode */}
          {editContent && <Collapse in={isEditing}>{editContent}</Collapse>}
        </CardContent>
      </Card>
  );

  if (embedded) return card;
  return <Grid item {...gridSize}>{card}</Grid>;
}
