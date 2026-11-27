import React from "react";
import { Box, LinearProgress, Typography } from "@mui/material";
import Module from "../Module";
import InfoCard from "../../common/InfoCard";
import InfoRow from "../../common/InfoRow";
import { SECTIONS } from "../../common/SectionTitle";
import { bytesToHumanReadable } from "../../../utils/format";
import { moduleScrollbarSx } from "../moduleConfig";

const usageCaptionSx = {
  fontSize: "calc(0.8rem * var(--module-scale, 1))",
  lineHeight: 1.2,
  fontWeight: 600,
  letterSpacing: 0.5,
  textTransform: "uppercase",
};

function UsageBar({ label, percent }) {
  const value = Number.isFinite(percent) ? Math.max(0, Math.min(100, percent)) : 0;
  return (
    <Box>
      <Box sx={{ display: "flex", justifyContent: "space-between", mb: 0.5 }}>
        <Typography variant="caption" color="text.secondary" sx={usageCaptionSx}>
          {label}
        </Typography>
        <Typography variant="caption" color="text.secondary" sx={usageCaptionSx}>
          {value.toFixed(1)}%
        </Typography>
      </Box>
      <LinearProgress
        variant="determinate"
        value={value}
        sx={{
          "&&": {
            height: 8,
            minHeight: 8,
            maxHeight: 8,
          },
          borderRadius: 1,
          backgroundColor: "background.paper",
          "& .MuiLinearProgress-bar": {
            height: 8,
            borderRadius: 1,
            backgroundColor: "primary.main",
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

const panelSx = {
  width: "100%",
  maxWidth: "100%",
  boxSizing: "border-box",
  minWidth: 0,
  flex: "0 0 auto",
  display: "flex",
  flexDirection: "column",
  justifyContent: "flex-start",
  gap: "calc(12px * var(--module-scale, 1))",
  p: "calc(12px * var(--module-scale, 1))",
  backgroundColor: "action.hover",
  borderRadius: 1,
};

function InfoPanel({ label, minBlock = false, children }) {
  return (
    <Box data-min-block={minBlock ? "" : undefined} sx={panelSx}>
      {label ? (
        <Typography color="text.secondary" sx={usageCaptionSx}>
          {label}
        </Typography>
      ) : null}
      {children}
    </Box>
  );
}

function MemoryBody({ deviceInfo }) {
  const free = deviceInfo?.free_heap;
  const total = deviceInfo?.total_heap;
  const used = deviceInfo?.used_heap;
  const ready = free !== undefined && total && used !== undefined;
  if (!ready) return <EmptyNote>No memory information available</EmptyNote>;
  return <Volume total={total} used={used} />;
}

const networkValueSize = "calc(0.95rem * var(--module-scale, 1))";

function NetworkBody({ deviceInfo, config }) {
  return (
    <InfoPanel>
      <InfoRow
        inline
        valueSize={networkValueSize}
        label="Device Name:"
        value={config?.deviceName || "espWiFi"}
      />
      <InfoRow
        inline
        valueSize={networkValueSize}
        label="Hostname:"
        value={deviceInfo?.hostname || "N/A"}
      />
      <InfoRow
        inline
        valueSize={networkValueSize}
        label="mDNS Hostname:"
        value={deviceInfo?.mdns || "N/A"}
      />
      <InfoRow
        inline
        valueSize={networkValueSize}
        label="IP Address:"
        value={deviceInfo?.ip || "N/A"}
      />
      <InfoRow
        inline
        valueSize={networkValueSize}
        label="MAC Address:"
        value={deviceInfo?.mac || "N/A"}
      />
    </InfoPanel>
  );
}

function Volume({
  label,
  total,
  used,
  minBlock = false,
  fields = ["total", "used", "free"],
}) {
  const free = total - used;
  const percent = total > 0 ? (used / total) * 100 : 0;
  const rows = {
    total: ["Total:", total],
    used: ["Used:", used],
    free: ["Free:", free],
  };
  return (
    <InfoPanel label={label} minBlock={minBlock}>
      {fields.map((key) => (
        <InfoRow
          key={key}
          inline
          label={rows[key][0]}
          value={bytesToHumanReadable(rows[key][1])}
        />
      ))}
      <UsageBar label="Usage" percent={percent} />
    </InfoPanel>
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
        minHeight: 0,
        overflow: "auto",
        ...moduleScrollbarSx,
        display: "flex",
        flexDirection: "column",
        justifyContent: "safe center",
        gap: "calc(12px * var(--module-scale, 1))",
      }}
    >
      {hasLfs && (
        <Volume
          minBlock
          label="LittleFS"
          total={deviceInfo.lfs_total}
          used={deviceInfo.lfs_used}
          fields={["used", "free", "total"]}
        />
      )}
      {hasSd && (
        <Volume
          minBlock={!hasLfs}
          label="SD Card"
          total={deviceInfo.sd_total}
          used={deviceInfo.sd_used}
          fields={["used", "free", "total"]}
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
        center
        hug={info !== "storage"}
        minHeight="auto"
        title={title}
        icon={section?.icon}
      >
        {body}
      </InfoCard>
    </Module>
  );
}
