/**
 * @file MemoryInfoCard.js
 * @brief Memory information card with usage visualization
 *
 * Displays free, total, and used heap with color-coded progress bar
 */

import React from "react";
import { Box, Typography, LinearProgress, Skeleton, Grid } from "@mui/material";
import MemoryIcon from "@mui/icons-material/Memory";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";
import { bytesToHumanReadable } from "../../utils/formatUtils";

/**
 * MemoryInfoCard Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.deviceInfo - Device information containing memory stats
 * @param {boolean} props.loading - Loading state
 * @returns {JSX.Element} The rendered memory info card
 */
export default function MemoryInfoCard({ deviceInfo, loading = false }) {
  const { free_heap, total_heap, used_heap } = deviceInfo || {};
  const hasMemoryInfo = free_heap !== undefined && total_heap && used_heap;
  const usagePercent = hasMemoryInfo ? (used_heap / total_heap) * 100 : 0;

  // Determine progress bar color based on usage
  const getProgressColor = () => {
    if (usagePercent > 80) return "error.main";
    if (usagePercent > 60) return "warning.main";
    return "success.main";
  };

  if (loading) {
    return (
      <InfoCard title="Memory" icon={MemoryIcon}>
        <Skeleton variant="text" width="100%" />
        <Skeleton variant="text" width="100%" />
        <Skeleton variant="rectangular" height={8} />
      </InfoCard>
    );
  }

  return (
    <InfoCard title="Memory" icon={MemoryIcon}>
      {hasMemoryInfo ? (
        <>
          <Grid container spacing={2}>
            <Grid item xs={12} sm={4}>
              <InfoRow
                label="Free Heap:"
                value={bytesToHumanReadable(free_heap)}
              />
            </Grid>
            <Grid item xs={12} sm={4}>
              <InfoRow
                label="Total Heap:"
                value={bytesToHumanReadable(total_heap)}
              />
            </Grid>
            <Grid item xs={12} sm={4}>
              <InfoRow
                label="Used Heap:"
                value={bytesToHumanReadable(used_heap)}
              />
            </Grid>
          </Grid>

          <Box sx={{ mt: 1 }}>
            <Box
              sx={{
                display: "flex",
                justifyContent: "space-between",
                mb: 0.5,
              }}
            >
              <Typography variant="caption" color="text.secondary">
                Memory Usage
              </Typography>
              <Typography variant="caption" color="text.secondary">
                {usagePercent.toFixed(1)}%
              </Typography>
            </Box>
            <LinearProgress
              variant="determinate"
              value={usagePercent}
              sx={{
                height: 8,
                borderRadius: 1,
                backgroundColor: "action.hover",
                "& .MuiLinearProgress-bar": {
                  borderRadius: 1,
                  backgroundColor: getProgressColor(),
                },
              }}
            />
          </Box>
        </>
      ) : (
        <Typography variant="body2" color="text.secondary">
          No memory information available
        </Typography>
      )}
    </InfoCard>
  );
}
