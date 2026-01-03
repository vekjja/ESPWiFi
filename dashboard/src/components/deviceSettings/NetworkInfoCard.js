/**
 * @file NetworkInfoCard.js
 * @brief Network information display card
 *
 * Displays IP address, mDNS hostname, and MAC address
 */

import React from "react";
import { Box, Typography } from "@mui/material";
import WifiIcon from "@mui/icons-material/Wifi";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";

/**
 * NetworkInfoCard Component
 *
 * @param {Object} props - Component props
 * @param {Object} props.deviceInfo - Device network information
 * @returns {JSX.Element} The rendered network info card
 */
export default function NetworkInfoCard({ deviceInfo }) {
  return (
    <InfoCard title="Network" icon={WifiIcon}>
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
      <InfoRow label="mDNS Hostname:" value={deviceInfo.mdns || "N/A"} />
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
    </InfoCard>
  );
}
