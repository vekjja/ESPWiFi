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
import DeveloperBoardIcon from "@mui/icons-material/DeveloperBoard";
import WifiIcon from "@mui/icons-material/Wifi";
import StorageIcon from "@mui/icons-material/Storage";
import SdCardIcon from "@mui/icons-material/SdCard";
import MemoryIcon from "@mui/icons-material/Memory";
import { bytesToHumanReadable, formatUptime } from "../../utils/formatUtils";
import { getRSSIChipColor, isValidRssi } from "../../utils/rssiUtils";

export default function DeviceSettingsInfoTab({
  deviceInfo,
  infoLoading,
  infoError,
  mode,
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
      {/* Network Information */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <WifiIcon color="primary" />
              Network
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  IP Address:
                </Typography>
                <Typography
                  variant="h6"
                  sx={{
                    fontWeight: 600,
                    color: "primary.main",
                    fontFamily: "monospace",
                  }}
                >
                  {deviceInfo.ip || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  mDNS Hostname:
                </Typography>
                <Typography variant="body1" sx={{ fontWeight: 500 }}>
                  {deviceInfo.mdns || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  MAC Address:
                </Typography>
                <Typography
                  variant="body1"
                  sx={{
                    fontWeight: 500,
                    fontFamily: "monospace",
                    fontSize: "0.9rem",
                  }}
                >
                  {deviceInfo.mac || "N/A"}
                </Typography>
              </Box>
              {/* Client mode: show connected network and RSSI */}
              {mode === "client" && deviceInfo.client_ssid && (
                <>
                  <Box
                    sx={{ display: "flex", justifyContent: "space-between" }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Connected Network:
                    </Typography>
                    <Typography variant="body1" sx={{ fontWeight: 500 }}>
                      {deviceInfo.client_ssid}
                    </Typography>
                  </Box>
                  {isValidRssi(deviceInfo.rssi) && (
                    <Box
                      sx={{ display: "flex", justifyContent: "space-between" }}
                    >
                      <Typography variant="body2" color="text.secondary">
                        Signal Strength:
                      </Typography>
                      <Chip
                        label={`${deviceInfo.rssi} dBm`}
                        color={getRSSIChipColor(deviceInfo.rssi)}
                        sx={{ fontWeight: 600 }}
                      />
                    </Box>
                  )}
                </>
              )}
              {/* AP mode: show access point name */}
              {(mode === "accessPoint" || mode === "ap") &&
                deviceInfo.ap_ssid && (
                  <Box
                    sx={{ display: "flex", justifyContent: "space-between" }}
                  >
                    <Typography variant="body2" color="text.secondary">
                      Access Point:
                    </Typography>
                    <Typography variant="body1" sx={{ fontWeight: 500 }}>
                      {deviceInfo.ap_ssid}
                    </Typography>
                  </Box>
                )}
            </Box>
          </CardContent>
        </Card>
      </Grid>

      {/* Memory Information */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <MemoryIcon color="primary" />
              Memory Usage
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

      {/* LittleFS Storage Information */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <StorageIcon color="primary" />
              Device Storage
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Total Storage:
                </Typography>
                <Typography
                  variant="body1"
                  sx={{ fontWeight: 600, color: "primary.main" }}
                >
                  {deviceInfo.littlefs_total
                    ? bytesToHumanReadable(deviceInfo.littlefs_total)
                    : "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Used Storage:
                </Typography>
                <Typography variant="body1" sx={{ fontWeight: 500 }}>
                  {deviceInfo.littlefs_used
                    ? bytesToHumanReadable(deviceInfo.littlefs_used)
                    : "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Free Storage:
                </Typography>
                <Typography variant="body1" sx={{ fontWeight: 500 }}>
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
                    <Typography
                      variant="h6"
                      sx={{ fontWeight: 600, color: "primary.main" }}
                    >
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
      {deviceInfo.sd_total && (
        <Grid item xs={12} md={6}>
          <Card sx={{ height: "100%", minHeight: 200 }}>
            <CardContent>
              <Typography
                variant="h6"
                gutterBottom
                sx={{ display: "flex", alignItems: "center", gap: 1 }}
              >
                <SdCardIcon color="primary" />
                SD Card Storage
              </Typography>
              <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    Total Storage:
                  </Typography>
                  <Typography
                    variant="body1"
                    sx={{ fontWeight: 600, color: "primary.main" }}
                  >
                    {deviceInfo.sd_total
                      ? bytesToHumanReadable(deviceInfo.sd_total)
                      : "Not Available"}
                  </Typography>
                </Box>
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    Used Storage:
                  </Typography>
                  <Typography variant="body1" sx={{ fontWeight: 500 }}>
                    {deviceInfo.sd_used
                      ? bytesToHumanReadable(deviceInfo.sd_used)
                      : "Not Available"}
                  </Typography>
                </Box>
                <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                  <Typography variant="body2" color="text.secondary">
                    Free Storage:
                  </Typography>
                  <Typography variant="body1" sx={{ fontWeight: 500 }}>
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
                      <Typography
                        variant="h6"
                        sx={{ fontWeight: 600, color: "primary.main" }}
                      >
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
      )}

      {/* Device Information */}
      <Grid item xs={12} md={6}>
        <Card sx={{ height: "100%", minHeight: 200 }}>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <DeveloperBoardIcon color="primary" />
              Board
            </Typography>
            <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Hostname:
                </Typography>
                <Typography
                  variant="body1"
                  sx={{ fontWeight: 600, color: "primary.main" }}
                >
                  {deviceInfo.hostname || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  Chip Model:
                </Typography>
                <Typography variant="body1" sx={{ fontWeight: 500 }}>
                  {deviceInfo.chip || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  SDK Version:
                </Typography>
                <Typography variant="body1" sx={{ fontWeight: 500 }}>
                  {deviceInfo.sdk_version || "N/A"}
                </Typography>
              </Box>
              <Box sx={{ display: "flex", justifyContent: "space-between" }}>
                <Typography variant="body2" color="text.secondary">
                  System Uptime:
                </Typography>
                <Typography variant="body1" sx={{ fontWeight: 500 }}>
                  {formatUptime(deviceInfo.uptime)}
                </Typography>
              </Box>
            </Box>
          </CardContent>
        </Card>
      </Grid>
    </Grid>
  );
}
