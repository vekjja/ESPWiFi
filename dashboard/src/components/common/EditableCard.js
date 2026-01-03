import React from "react";
import {
  Card,
  CardContent,
  Box,
  Typography,
  IconButton,
  Tooltip,
} from "@mui/material";
import EditIcon from "@mui/icons-material/Edit";

/**
 * Reusable card component with optional edit functionality
 * Follows Material Design and React best practices for composition
 *
 * @param {Object} props - Component props
 * @param {string} props.title - Card title
 * @param {React.ReactNode} props.icon - Icon component to display
 * @param {React.ReactNode} props.children - Card content
 * @param {boolean} props.editable - Whether card has edit functionality
 * @param {boolean} props.isEditing - Current edit state
 * @param {Function} props.onEdit - Callback when edit button is clicked
 * @param {Object} props.sx - Additional sx prop for styling
 * @param {Object} props.cardSx - Additional sx prop for Card styling
 */
export default function EditableCard({
  title,
  icon: Icon,
  children,
  editable = false,
  isEditing = false,
  onEdit,
  sx = {},
  cardSx = {},
}) {
  return (
    <Card sx={{ height: "100%", ...cardSx }}>
      <CardContent>
        <Box
          sx={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
            mb: 2,
            ...sx,
          }}
        >
          <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
            {Icon && <Icon color="primary" />}
            <Typography variant="h6">{title}</Typography>
          </Box>
          {editable && !isEditing && onEdit && (
            <Tooltip title={`Edit ${title}`}>
              <IconButton onClick={onEdit} size="small">
                <EditIcon />
              </IconButton>
            </Tooltip>
          )}
        </Box>
        {children}
      </CardContent>
    </Card>
  );
}
