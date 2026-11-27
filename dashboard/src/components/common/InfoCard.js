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

/** Card surface shared by settings sections and system-info modules. */
export const infoCardSurfaceSx = {
  backgroundColor: "background.paper",
  backgroundImage:
    "linear-gradient(rgba(255, 255, 255, 0.05), rgba(255, 255, 255, 0.05))",
  border: "none",
  borderRadius: "3px",
  boxShadow: 1,
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
}) {
  const card = (
      <Card
        sx={{
          height: embedded ? "auto" : "100%",
          width: "100%",
          maxWidth: "100%",
          boxSizing: "border-box",
          minWidth: 0,
          minHeight,
          ...infoCardSurfaceSx,
        }}
      >
        <CardContent>
          <Box
            sx={{
              display: "flex",
              justifyContent: "space-between",
              alignItems: "center",
              mb: 2,
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

          {/* Edit Mode */}
          {editContent && <Collapse in={isEditing}>{editContent}</Collapse>}
        </CardContent>
      </Card>
  );

  if (embedded) return card;
  return <Grid item {...gridSize}>{card}</Grid>;
}
