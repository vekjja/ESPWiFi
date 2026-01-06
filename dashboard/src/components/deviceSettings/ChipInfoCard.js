/**
 * @file ChipInfoCard.js
 * @brief Chip information display card
 *
 * Displays chip model + ESP-IDF SDK version from /api/info.
 */

import React from "react";
import { Grid } from "@mui/material";
import MemoryIcon from "@mui/icons-material/Memory";
import InfoCard from "../common/InfoCard";
import InfoRow from "../common/InfoRow";

export default function ChipInfoCard({ deviceInfo }) {
  return (
    <InfoCard title="Chip" icon={MemoryIcon}>
      <Grid container spacing={2}>
        <Grid item xs={12} sm={4}>
          <InfoRow label="Chip:" value={deviceInfo?.chip || "N/A"} />
        </Grid>
        <Grid item xs={12} sm={4}>
          <InfoRow label="espWiFi:" value={deviceInfo?.fw_version || "N/A"} />
        </Grid>
        <Grid item xs={12} sm={4}>
          <InfoRow label="ESP-IDF:" value={deviceInfo?.sdk_version || "N/A"} />
        </Grid>
      </Grid>
    </InfoCard>
  );
}
