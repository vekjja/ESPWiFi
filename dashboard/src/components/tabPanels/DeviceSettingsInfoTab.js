import React from "react";
import {
  Box,
  Alert,
  Typography,
  Card,
  CardContent,
  Grid,
  Chip,
  Skeleton,
} from "@mui/material";
import InfoIcon from "@mui/icons-material/Info";
import { bytesToHumanReadable, formatUptime } from "../../utils/formatUtils";

export default function DeviceSettingsInfoTab({
  deviceInfo,
  infoLoading,
  infoError,
}) {
  if (infoLoading) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Box sx={{ textAlign: "center" }}>
          <Skeleton
            variant="circular"
            width={40}
            height={40}
            sx={{ mx: "auto", mb: 2 }}
          />
          <Skeleton variant="text" width={200} height={24} sx={{ mb: 1 }} />
          <Skeleton variant="text" width={150} height={20} />
        </Box>
      </Box>
    );
  }

  if (infoError) {
    return (
      <Alert severity="error" sx={{ marginBottom: 2 }}>
        {infoError}
      </Alert>
    );
  }

  if (!deviceInfo) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Typography color="text.secondary">
          No device information available
        </Typography>
      </Box>
    );
  }

  return (
    <Grid container spacing={2}>
      {/* Device Information */}
      <Grid item xs={12} md={6}>
        <Card>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <InfoIcon color="primary" />
              Device Information
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Hostname:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.hostname || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  mDNS:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.mdns || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Chip:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.chip || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  SDK Version:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.sdk_version || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Uptime:
                </Typography>
                <Typography variant="body2">
                  {formatUptime(deviceInfo.uptime)}
                </Typography>
              </Box>
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* Network Information */}
      <Grid item xs={12} md={6}>
        <Card>
          <CardContent>
            <Typography variant="h6" gutterBottom>
              Network Information
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  IP Address:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.ip || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  MAC Address:
                </Typography>
                <Typography
                  variant="body2"
                  sx={{ fontFamily: "monospace", fontSize: "0.75rem" }}
                >
                  {deviceInfo.mac || "N/A"}
                </Typography>
              </Box>
              {deviceInfo.client_ssid && (
                <Box
                  sx={{
                    display: "flex",
                    justifyContent: "space-between",
                  }}
                >
                  <Typography variant="body2" color="text.secondary">
                    Connected SSID:
                  </Typography>
                  <Typography variant="body2">
                    {deviceInfo.client_ssid}
                  </Typography>
                </Box>
              )}
              {deviceInfo.rssi && (
                <Box
                  sx={{
                    display: "flex",
                    justifyContent: "space-between",
                  }}
                >
                  <Typography variant="body2" color="text.secondary">
                    Signal Strength:
                  </Typography>
                  <Chip
                    label={`${deviceInfo.rssi} dBm`}
                    color={
                      deviceInfo.rssi > -50
                        ? "success"
                        : deviceInfo.rssi > -70
                        ? "warning"
                        : "error"
                    }
                    size="small"
                  />
                </Box>
              )}
              {deviceInfo.ap_ssid && (
                <Box
                  sx={{
                    display: "flex",
                    justifyContent: "space-between",
                  }}
                >
                  <Typography variant="body2" color="text.secondary">
                    AP SSID:
                  </Typography>
                  <Typography variant="body2">{deviceInfo.ap_ssid}</Typography>
                </Box>
              )}
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* LittleFS Storage Information */}
      <Grid item xs={12} md={6}>
        <Card>
          <CardContent>
            <Typography variant="h6" gutterBottom>
              Device Storage
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Total:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.littlefs_total
                    ? bytesToHumanReadable(deviceInfo.littlefs_total)
                    : "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Used:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.littlefs_used
                    ? bytesToHumanReadable(deviceInfo.littlefs_used)
                    : "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Free:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.littlefs_free
                    ? bytesToHumanReadable(deviceInfo.littlefs_free)
                    : "N/A"}
                </Typography>
              </Box>
              {deviceInfo.littlefs_total && deviceInfo.littlefs_used && (
                <Box sx={{ mt: 1 }}>
                  <Box
                    sx={{
                      display: "flex",
                      justifyContent: "space-between",
                      mb: 0.5,
                    }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Usage:
                    </Typography>
                    <Typography variant="body2">
                      {Math.round(
                        (deviceInfo.littlefs_used / deviceInfo.littlefs_total) *
                          100
                      )}
                      %
                    </Typography>
                  </Box>
                  <Box
                    sx={{
                      width: "100%",
                      height: 8,
                      backgroundColor: "grey.300",
                      borderRadius: 1,
                      overflow: "hidden",
                    }}
                  >
                    <Box
                      sx={{
                        width: `${
                          (deviceInfo.littlefs_used /
                            deviceInfo.littlefs_total) *
                          100
                        }%`,
                        height: "100%",
                        backgroundColor: "primary.main",
                      }}
                    />
                  </Box>
                </Box>
              )}
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* SD Card Storage Information */}
      <Grid item xs={12} md={6}>
        <Card>
          <CardContent>
            <Typography variant="h6" gutterBottom>
              SD Card Storage
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box
                sx={{
                  display: "flex",
                  justifyContent: "space-between",
                }}
              >
                <Typography variant="body2" color="text.secondary">
                  Total:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.sd_total
                    ? bytesToHumanReadable(deviceInfo.sd_total)
                    : "Not Available"}
                </Typography>
              </Box>
              <Box
                sx={{
                  display: "flex",
                  justifyContent: "space-between",
                }}
              >
                <Typography variant="body2" color="text.secondary">
                  Used:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.sd_used
                    ? bytesToHumanReadable(deviceInfo.sd_used)
                    : "Not Available"}
                </Typography>
              </Box>
              <Box
                sx={{
                  display: "flex",
                  justifyContent: "space-between",
                }}
              >
                <Typography variant="body2" color="text.secondary">
                  Free:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.sd_free
                    ? bytesToHumanReadable(deviceInfo.sd_free)
                    : "Not Available"}
                </Typography>
              </Box>
              {deviceInfo.sd_total && deviceInfo.sd_used && (
                <Box sx={{ mt: 1 }}>
                  <Box
                    sx={{
                      display: "flex",
                      justifyContent: "space-between",
                      mb: 0.5,
                    }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Usage:
                    </Typography>
                    <Typography variant="body2">
                      {Math.round(
                        (deviceInfo.sd_used / deviceInfo.sd_total) * 100
                      )}
                      %
                    </Typography>
                  </Box>
                  <Box
                    sx={{
                      width: "100%",
                      height: 8,
                      backgroundColor: "grey.300",
                      borderRadius: 1,
                      overflow: "hidden",
                    }}
                  >
                    <Box
                      sx={{
                        width: `${
                          (deviceInfo.sd_used / deviceInfo.sd_total) * 100
                        }%`,
                        height: "100%",
                        backgroundColor: "primary.main",
                      }}
                    />
                  </Box>
                </Box>
              )}
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* Memory Information */}
      <Grid item xs={12} md={6}>
        <Card>
          <CardContent>
            <Typography variant="h6" gutterBottom>
              Memory Information
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Total Heap:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.total_heap
                    ? bytesToHumanReadable(deviceInfo.total_heap)
                    : "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Used:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.used_heap
                    ? bytesToHumanReadable(deviceInfo.used_heap)
                    : "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Free:
                </Typography>
                <Typography variant="body2">
                  {deviceInfo.free_heap
                    ? bytesToHumanReadable(deviceInfo.free_heap)
                    : "N/A"}
                </Typography>
              </Box>
              {deviceInfo.total_heap && deviceInfo.used_heap && (
                <Box sx={{ mt: 1 }}>
                  <Box
                    sx={{
                      display: "flex",
                      justifyContent: "space-between",
                      mb: 0.5,
                    }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Usage:
                    </Typography>
                    <Typography variant="body2">
                      {Math.round(
                        (deviceInfo.used_heap / deviceInfo.total_heap) * 100
                      )}
                      %
                    </Typography>
                  </Box>
                  <Box
                    sx={{
                      width: "100%",
                      height: 8,
                      backgroundColor: "grey.300",
                      borderRadius: 1,
                      overflow: "hidden",
                    }}
                  >
                    <Box
                      sx={{
                        width: `${
                          (deviceInfo.used_heap / deviceInfo.total_heap) * 100
                        }%`,
                        height: "100%",
                        backgroundColor: "primary.main",
                      }}
                    />
                  </Box>
                </Box>
              )}
            </Box>
          </CardContent>
        </Card>
      </Grid>
    </Grid>
  );
}
