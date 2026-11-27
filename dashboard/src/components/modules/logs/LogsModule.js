import React, { useEffect, useRef } from "react";
import DescriptionIcon from "@mui/icons-material/Description";
import Module from "../Module";
import LogsSettingsModal from "../../toolbar/logs/LogsSettingsModal";

// A card that grew with the log text saved that content height. Anything
// taller than a normal dashboard card is that dump, not a chosen size.
const MAX_SAVED_LOG_HEIGHT = 2400;
const DEFAULT_LOG_HEIGHT = 480;

export default function LogsModule({
  module,
  config,
  deviceOnline = true,
  saveConfig,
  saveConfigToDevice,
  onDelete,
  onUpdate,
  controlConnected = false,
  logsText = "",
  logsError = "",
  onRequestLogs = null,
}) {
  const savedHeight = Number(module?.height);
  const runaway =
    module?.size === "custom" && savedHeight > MAX_SAVED_LOG_HEIGHT;
  const view = runaway ? { ...module, height: DEFAULT_LOG_HEIGHT } : module;
  const corrected = useRef(false);

  useEffect(() => {
    if (!runaway || corrected.current) return;
    corrected.current = true;
    const width = Number(module.width) > 0 ? Math.round(Number(module.width)) : 720;
    onUpdate?.(module.key, {
      size: "custom",
      width,
      height: DEFAULT_LOG_HEIGHT,
    });
  }, [runaway, module, onUpdate]);

  return (
    <Module
      moduleConfig={view}
      title="Logs"
      icon={DescriptionIcon}
      online={deviceOnline}
      onDelete={() => onDelete(module.key)}
      deleteTooltip="Remove"
      onResize={({ width, height }) =>
        onUpdate?.(module.key, { size: "custom", width, height })
      }
      onToggleWide={() => onUpdate?.(module.key, { wide: !module.wide })}
      sx={{ boxSizing: "border-box" }}
    >
      <LogsSettingsModal
        embedded
        open
        config={config}
        saveConfig={saveConfig}
        saveConfigToDevice={saveConfigToDevice}
        controlConnected={controlConnected || deviceOnline}
        logsText={logsText}
        logsError={logsError}
        onRequestLogs={onRequestLogs}
        onMoveToToolbar={() => onDelete(module.key)}
      />
    </Module>
  );
}
