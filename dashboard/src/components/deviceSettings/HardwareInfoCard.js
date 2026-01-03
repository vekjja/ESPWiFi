import React from "react";
import {
  Box,
  TextField,
  IconButton,
  Tooltip,
  Typography,
  Skeleton,
} from "@mui/material";
import DeveloperBoardIcon from "@mui/icons-material/DeveloperBoard";
import SaveIcon from "@mui/icons-material/Save";
import CancelIcon from "@mui/icons-material/Cancel";
import EditableCard from "../common/EditableCard";
import InfoRow from "../common/InfoRow";
import { useEditableField } from "../../hooks/useEditableField";
import { formatUptime } from "../../utils/formatUtils";

/**
 * Hardware information card with editable device name
 * Displays chip model, IDF version, uptime, and device name
 *
 * @param {Object} props - Component props
 * @param {Object} props.deviceInfo - Device hardware information
 * @param {string} props.deviceName - Current device name from config
 * @param {Function} props.onSaveDeviceName - Callback to save device name
 * @param {boolean} props.loading - Loading state
 */
export default function HardwareInfoCard({
  deviceInfo,
  deviceName,
  onSaveDeviceName,
  loading = false,
}) {
  const {
    isEditing,
    tempValue,
    setTempValue,
    startEditing,
    cancelEditing,
    saveEditing,
  } = useEditableField(deviceName, onSaveDeviceName);

  if (loading) {
    return (
      <EditableCard title="Hardware" icon={DeveloperBoardIcon}>
        <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
          <Skeleton variant="text" width="100%" />
          <Skeleton variant="text" width="80%" />
          <Skeleton variant="text" width="90%" />
        </Box>
      </EditableCard>
    );
  }

  return (
    <EditableCard
      title="Hardware"
      icon={DeveloperBoardIcon}
      editable
      isEditing={isEditing}
      onEdit={startEditing}
    >
      <Box sx={{ display: "flex", flexDirection: "column", gap: 1 }}>
        {/* Device Name - Editable */}
        <Box
          sx={{
            display: "flex",
            justifyContent: "space-between",
            alignItems: "center",
          }}
        >
          <Typography variant="body2" color="text.secondary">
            Device Name:
          </Typography>
          {!isEditing ? (
            <Typography
              variant="body1"
              sx={{ fontWeight: 600, color: "primary.main" }}
            >
              {deviceName || "ESPWiFi"}
            </Typography>
          ) : (
            <Box
              sx={{
                display: "flex",
                gap: 0.5,
                alignItems: "center",
                flex: 1,
                ml: 1,
              }}
            >
              <TextField
                fullWidth
                size="small"
                value={tempValue}
                onChange={(e) => setTempValue(e.target.value)}
                placeholder="Device name"
              />
              <Tooltip title="Save">
                <IconButton onClick={saveEditing} color="primary" size="small">
                  <SaveIcon />
                </IconButton>
              </Tooltip>
              <Tooltip title="Cancel">
                <IconButton onClick={cancelEditing} size="small">
                  <CancelIcon />
                </IconButton>
              </Tooltip>
            </Box>
          )}
        </Box>

        {deviceInfo?.chip && (
          <InfoRow label="Chip Model:" value={deviceInfo.chip} />
        )}
        {deviceInfo?.sdk_version && (
          <InfoRow label="ESP-IDF Version:" value={deviceInfo.sdk_version} />
        )}
        {deviceInfo?.uptime !== undefined && (
          <InfoRow label="Uptime:" value={formatUptime(deviceInfo.uptime)} />
        )}
      </Box>
    </EditableCard>
  );
}
