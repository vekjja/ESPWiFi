import React from "react";
import { Box, LinearProgress, Typography } from "@mui/material";
import Module from "../Module";
import InfoCard from "../../common/InfoCard";
import InfoRow from "../../common/InfoRow";
import SectionTitle, { SECTIONS } from "../../common/SectionTitle";
import { bytesToHumanReadable } from "../../../utils/format";

function usageColor(percent) {
  if (percent > 80) return "error.main";
  if (percent > 60) return "warning.main";
  return "success.main";
}

function UsageBar({ label, percent }) {
  const value = Number.isFinite(percent) ? Math.max(0, Math.min(100, percent)) : 0;
  return (
    <Box sx={{ mt: 1 }}>
      <Box sx={{ display: "flex", justifyContent: "space-between", mb: 0.5 }}>
        <Typography variant="caption" color="text.secondary">
          {label}
        </Typography>
        <Typography variant="caption" color="text.secondary">
          {value.toFixed(1)}%
        </Typography>
      </Box>
      <LinearProgress
        variant="determinate"
        value={value}
        sx={{
          height: 8,
          borderRadius: 1,
          backgroundColor: "action.hover",
          "& .MuiLinearProgress-bar": {
            borderRadius: 1,
            backgroundColor: usageColor(value),
          },
        }}
      />
    </Box>
  );
}

function EmptyNote({ children }) {
  return (
    <Typography variant="body2" color="text.secondary" sx={{ py: 1 }}>
      {children}
    </Typography>
  );
}

function MemoryBody({ deviceInfo }) {
  const free = deviceInfo?.free_heap;
  const total = deviceInfo?.total_heap;
  const used = deviceInfo?.used_heap;
  const ready = free !== undefined && total && used !== undefined;
  if (!ready) return <EmptyNote>No memory information available</EmptyNote>;
  const percent = (used / total) * 100;
  return (
    <Box
      sx={{
        width: "100%",
        flex: "1 1 auto",
        minHeight: 0,
        display: "flex",
        flexDirection: "column",
        justifyContent: "space-between",
        gap: "calc(12px * var(--module-scale, 1))",
      }}
    >
      <InfoRow label="Used Heap:" value={bytesToHumanReadable(used)} />
      <InfoRow label="Free Heap:" value={bytesToHumanReadable(free)} />
      <InfoRow label="Total Heap:" value={bytesToHumanReadable(total)} />
      <UsageBar label="Memory Usage" percent={percent} />
    </Box>
  );
}

function NetworkBody({ deviceInfo, config }) {
  return (
    <Box
      sx={{
        width: "100%",
        flex: "1 1 auto",
        minHeight: 0,
        display: "flex",
        flexDirection: "column",
        justifyContent: "space-between",
        gap: "calc(12px * var(--module-scale, 1))",
      }}
    >
      <InfoRow label="Device Name:" value={config?.deviceName || "ESPWiFi"} />
      <InfoRow label="Hostname:" value={deviceInfo?.hostname || "N/A"} />
      <InfoRow label="mDNS Hostname:" value={deviceInfo?.mdns || "N/A"} />
      <InfoRow label="IP Address:" value={deviceInfo?.ip || "N/A"} />
      <InfoRow label="MAC Address:" value={deviceInfo?.mac || "N/A"} />
    </Box>
  );
}

function Volume({ section, total, used }) {
  const free = total - used;
  const percent = total > 0 ? (used / total) * 100 : 0;
  return (
    <Box
      sx={{
        width: "100%",
        maxWidth: "100%",
        boxSizing: "border-box",
        minWidth: 0,
        flex: "1 1 auto",
        display: "flex",
        flexDirection: "column",
        justifyContent: "space-between",
        gap: "calc(12px * var(--module-scale, 1))",
        p: "calc(12px * var(--module-scale, 1))",
        backgroundColor: "action.hover",
        borderRadius: 1,
      }}
    >
      <SectionTitle
        {...section}
        variant="subtitle2"
        iconColor="inherit"
        iconSize="small"
        sx={{ mb: 0.5 }}
      />
      <InfoRow label="Total:" value={bytesToHumanReadable(total)} />
      <InfoRow label="Used:" value={bytesToHumanReadable(used)} />
      <InfoRow label="Free:" value={bytesToHumanReadable(free)} />
      <UsageBar label="Usage" percent={percent} />
    </Box>
  );
}

function StorageBody({ deviceInfo }) {
  const hasLfs =
    deviceInfo?.lfs_total !== undefined && deviceInfo?.lfs_used !== undefined;
  const hasSd =
    deviceInfo?.sd_total !== undefined && deviceInfo?.sd_used !== undefined;
  if (!hasLfs && !hasSd) {
    return <EmptyNote>No storage information available</EmptyNote>;
  }
  return (
    <Box
      sx={{
        width: "100%",
        maxWidth: "100%",
        boxSizing: "border-box",
        minWidth: 0,
        flex: "1 1 auto",
        display: "flex",
        flexDirection: "column",
        justifyContent: "space-between",
        gap: "calc(12px * var(--module-scale, 1))",
      }}
    >
      {hasLfs && (
        <Volume
          section={SECTIONS.littlefs}
          total={deviceInfo.lfs_total}
          used={deviceInfo.lfs_used}
        />
      )}
      {hasSd && (
        <Volume
          section={SECTIONS.sd}
          total={deviceInfo.sd_total}
          used={deviceInfo.sd_used}
        />
      )}
    </Box>
  );
}

export default function SystemInfoModule({
  module,
  config,
  deviceInfo,
  deviceOnline = true,
  onDelete,
  onUpdate,
}) {
  const info = module?.info;
  const section = SECTIONS[info];
  const title = section?.title || "System Info";
  const customSize = module?.size === "custom" && Number(module.width) > 0;

  let body = <EmptyNote>Unknown system info</EmptyNote>;
  if (!deviceInfo && deviceOnline) {
    body = <EmptyNote>Waiting for device info</EmptyNote>;
  } else if (info === "memory") {
    body = <MemoryBody deviceInfo={deviceInfo} />;
  } else if (info === "network") {
    body = <NetworkBody deviceInfo={deviceInfo} config={config} />;
  } else if (info === "storage") {
    body = <StorageBody deviceInfo={deviceInfo} />;
  }

  return (
    <Module
      moduleConfig={module}
      online={deviceOnline}
      onDelete={() => onDelete(module.key)}
      deleteTooltip="Remove"
      deletePlacement="start"
      onResize={({ width, height }) =>
        onUpdate?.(module.key, { size: "custom", width, height })
      }
      onToggleWide={() => onUpdate?.(module.key, { wide: !module.wide })}
      sx={{
        ...(customSize
          ? null
          : {
              width: "calc(100% - 20px)",
              maxWidth: "calc(100% - 20px)",
              minWidth: 0,
            }),
        boxSizing: "border-box",
        "& > .MuiCardContent-root": {
          minHeight: "auto",
          paddingBottom: "8px !important",
        },
      }}
    >
      <InfoCard
        embedded
        fluid
        scale
        minHeight="auto"
        title={title}
        icon={section?.icon}
      >
        {body}
      </InfoCard>
    </Module>
  );
}
