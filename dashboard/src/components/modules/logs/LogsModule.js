import React from "react";
import DescriptionIcon from "@mui/icons-material/Description";
import Module from "../Module";
import LogsSettingsModal from "../../toolbar/logs/LogsSettingsModal";

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
  return (
    <Module
      moduleConfig={module}
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
