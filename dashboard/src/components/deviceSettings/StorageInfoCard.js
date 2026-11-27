/**
 * @file StorageInfoCard.js
 * @brief Storage information card for LittleFS and SD card
 * 
 * Displays storage usage with color-coded progress bars
 */

import React from "react";
import {
  Box,
  Typography,
  Card,
  CardContent,
  Grid,
  LinearProgress,
  Skeleton,
} from "@mui/material";
import InfoRow from "../common/InfoRow";
import { infoCardSurfaceSx } from "../common/InfoCard";
import SectionTitle, { SECTIONS } from "../common/SectionTitle";
import { bytesToHumanReadable } from "../../utils/format";

/**
 * StorageInfoCard Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.deviceInfo - Device information containing storage stats
 * @param {boolean} props.loading - Loading state
 * @returns {JSX.Element} The rendered storage info card
 */
export default function StorageInfoCard({ deviceInfo, loading = false }) {
  // Determine progress bar color based on usage
  const getProgressColor = (percent) => {
    if (percent > 80) return "error.main";
    if (percent > 60) return "warning.main";
    return "success.main";
  };

  const hasLfsInfo =
    deviceInfo?.lfs_total !== undefined && deviceInfo?.lfs_used !== undefined;
  const hasSdInfo =
    deviceInfo?.sd_total !== undefined && deviceInfo?.sd_used !== undefined;

  const lfsPercent = hasLfsInfo
    ? (deviceInfo.lfs_used / deviceInfo.lfs_total) * 100
    : 0;
  const sdPercent = hasSdInfo
    ? (deviceInfo.sd_used / deviceInfo.sd_total) * 100
    : 0;

  if (loading) {
    return (
      <Grid item xs={12}>
        <Card sx={infoCardSurfaceSx}>
          <CardContent sx={{ pb: 3 }}>
            <SectionTitle {...SECTIONS.storage} sx={{ mb: 1 }} />
            <Skeleton variant="text" width="100%" />
            <Skeleton variant="text" width="100%" />
            <Skeleton variant="rectangular" height={8} />
          </CardContent>
        </Card>
      </Grid>
    );
  }

  return (
    <Grid item xs={12}>
      <Card sx={infoCardSurfaceSx}>
        <CardContent sx={{ pb: 3 }}>
          <SectionTitle {...SECTIONS.storage} sx={{ mb: 2 }} />

          <Grid container spacing={2} sx={{ mt: 0.5 }}>
            {/* LittleFS Storage */}
            {hasLfsInfo && (
              <Grid item xs={12}>
                <Box
                  sx={{
                    display: "flex",
                    flexDirection: "column",
                    gap: 1,
                    p: 2,
                    backgroundColor: "action.hover",
                    borderRadius: 1,
                  }}
                >
                  <SectionTitle
                    {...SECTIONS.littlefs}
                    variant="subtitle2"
                    iconColor="inherit"
                    iconSize="small"
                    sx={{ mb: 0.5 }}
                  />
                  <Grid container spacing={2}>
                    <Grid item xs={12}>
                      <InfoRow
                        label="Used:"
                        value={bytesToHumanReadable(deviceInfo.lfs_used)}
                      />
                    </Grid>
                    <Grid item xs={12}>
                      <InfoRow
                        label="Free:"
                        value={bytesToHumanReadable(
                          deviceInfo.lfs_total - deviceInfo.lfs_used
                        )}
                      />
                    </Grid>
                    <Grid item xs={12}>
                      <InfoRow
                        label="Total:"
                        value={bytesToHumanReadable(deviceInfo.lfs_total)}
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
                        Usage
                      </Typography>
                      <Typography variant="caption" color="text.secondary">
                        {lfsPercent.toFixed(1)}%
                      </Typography>
                    </Box>
                    <LinearProgress
                      variant="determinate"
                      value={lfsPercent}
                      sx={{
                        height: 8,
                        borderRadius: 1,
                        backgroundColor: "action.selected",
                        "& .MuiLinearProgress-bar": {
                          borderRadius: 1,
                          backgroundColor: getProgressColor(lfsPercent),
                        },
                      }}
                    />
                  </Box>
                </Box>
              </Grid>
            )}

            {/* SD Card Storage */}
            {hasSdInfo && (
              <Grid item xs={12}>
                <Box
                  sx={{
                    display: "flex",
                    flexDirection: "column",
                    gap: 1,
                    p: 2,
                    backgroundColor: "action.hover",
                    borderRadius: 1,
                  }}
                >
                  <SectionTitle
                    {...SECTIONS.sd}
                    variant="subtitle2"
                    iconColor="inherit"
                    iconSize="small"
                    sx={{ mb: 0.5 }}
                  />
                  <Grid container spacing={2}>
                    <Grid item xs={12}>
                      <InfoRow
                        label="Used:"
                        value={bytesToHumanReadable(deviceInfo.sd_used)}
                      />
                    </Grid>
                    <Grid item xs={12}>
                      <InfoRow
                        label="Free:"
                        value={bytesToHumanReadable(
                          deviceInfo.sd_total - deviceInfo.sd_used
                        )}
                      />
                    </Grid>
                    <Grid item xs={12}>
                      <InfoRow
                        label="Total:"
                        value={bytesToHumanReadable(deviceInfo.sd_total)}
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
                        Usage
                      </Typography>
                      <Typography variant="caption" color="text.secondary">
                        {sdPercent.toFixed(1)}%
                      </Typography>
                    </Box>
                    <LinearProgress
                      variant="determinate"
                      value={sdPercent}
                      sx={{
                        height: 8,
                        borderRadius: 1,
                        backgroundColor: "action.selected",
                        "& .MuiLinearProgress-bar": {
                          borderRadius: 1,
                          backgroundColor: getProgressColor(sdPercent),
                        },
                      }}
                    />
                  </Box>
                </Box>
              </Grid>
            )}

            {!hasLfsInfo && !hasSdInfo && (
              <Grid item xs={12}>
                <Typography variant="body2" color="text.secondary">
                  No storage information available
                </Typography>
              </Grid>
            )}
          </Grid>
        </CardContent>
      </Card>
    </Grid>
  );
}

