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
  Typography,
  IconButton,
  Tooltip,
  Collapse,
  Grid,
} from "@mui/material";
import EditIcon from "@mui/icons-material/Edit";

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
  editable = false,
  isEditing = false,
  onEdit,
  children,
  editContent,
  gridSize = { xs: 12, md: 6 },
  minHeight = 200,
}) {
  return (
    <Grid item {...gridSize}>
      <Card sx={{ height: "100%", minHeight }}>
        <CardContent>
          <Box
            sx={{
              display: "flex",
              justifyContent: "space-between",
              alignItems: "center",
              mb: 2,
            }}
          >
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
              {Icon && <Icon color="primary" />}
              <Typography variant="h6">{title}</Typography>
            </Box>
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
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1.5 }}>
              {children}
            </Box>
          </Collapse>

          {/* Edit Mode */}
          {editContent && <Collapse in={isEditing}>{editContent}</Collapse>}
        </CardContent>
      </Card>
    </Grid>
  );
}
