/**
 * @file NetworkInfoCard.js
 * @brief Network information display card
 *
 * Displays IP address, mDNS hostname, and MAC address
 */

import React from "react";
import { Box, Typography, Grid } from "@mui/material";
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
      <Grid container spacing={2}>
        <Grid item xs={12} sm={4}>
          <InfoRow label="IP Address:" value={deviceInfo.ip || "N/A"} />
        </Grid>
        <Grid item xs={12} sm={4}>
          <InfoRow label="mDNS Hostname:" value={deviceInfo.mdns || "N/A"} />
        </Grid>
        <Grid item xs={12} sm={4}>
          <InfoRow label="MAC Address:" value={deviceInfo.mac || "N/A"} />
        </Grid>
      </Grid>
    </InfoCard>
  );
}
