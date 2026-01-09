/**
 * Device Settings Modal Component
 * Manages device configuration including network, auth, OTA, and device info
 * Organized into tabs for better UX and maintainability
 */

import React, { useState, useEffect, useCallback } from "react";
import { Box } from "@mui/material";
import RestartIcon from "@mui/icons-material/RestartAlt";
import LogoutIcon from "@mui/icons-material/Logout";
import SettingsIcon from "@mui/icons-material/Settings";
import CloseIcon from "@mui/icons-material/Close";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";
import DeviceSettingsInfoTab from "./deviceSettings/DeviceSettingsInfoTab";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";
import { clearAuthToken } from "../utils/authUtils";

export default function DeviceSettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
  cloudMode = false,
  deviceInfoOverride = null,
}) {
  // Device info state
  const [deviceInfo, setDeviceInfo] = useState(null);
  const [infoLoading, setInfoLoading] = useState(false);
  const [infoError, setInfoError] = useState("");

  /**
   * Initialize state from config when it changes
   */
  useEffect(() => {
    if (config) {
      // Config loaded
    }
  }, [config]);

  /**
   * Handle modal open - fetch device info
   */
  const handleOpenModal = useCallback(() => {
    if (deviceInfoOverride) {
      setDeviceInfo(deviceInfoOverride || null);
      setInfoError("");
      setInfoLoading(false);
      return;
    }
    if (cloudMode) {
      setDeviceInfo(null);
      setInfoError("Control tunnel not connected");
      setInfoLoading(false);
      return;
    }
    fetchDeviceInfo();
  }, [cloudMode, deviceInfoOverride]);

  /**
   * Handle modal close
   */
  const handleCloseModal = useCallback(() => {
    if (onClose) onClose();
  }, [onClose]);

  /**
   * Device info is sourced from the control WebSocket (cmd=get_info) and passed
   * in via deviceInfoOverride.
   */
  const fetchDeviceInfo = async () => {
    setInfoLoading(true);
    try {
      if (deviceInfoOverride) {
        setDeviceInfo(deviceInfoOverride);
        setInfoError("");
      } else {
        setDeviceInfo(null);
        setInfoError(
          cloudMode
            ? "Control tunnel not connected"
            : "Control WebSocket not connected"
        );
      }
    } finally {
      setInfoLoading(false);
    }
  };

  // Wrap saves so the info tab updates immediately after config changes.
  const saveConfigToDeviceAndRefresh = useCallback(
    async (newConfig) => {
      try {
        const p = saveConfigToDevice?.(newConfig);
        // If saveConfigToDevice returns a promise, wait for it.
        if (p && typeof p.then === "function") {
          await p;
        }
      } finally {
        // Refresh runtime info (cloud tunnel connected/registered, etc).
        if (cloudMode) {
          setDeviceInfo(deviceInfoOverride || null);
        } else {
          await fetchDeviceInfo();
        }
      }
    },
    [saveConfigToDevice, cloudMode, deviceInfoOverride]
  );

  /**
   * Restart device
   */
  const handleRestart = useCallback(() => {
    // In dev, buildApiUrl() defaults to localhost unless we pass a device host.
    // Always target the device.
    const restartUrl = buildApiUrl("/api/restart", config?.deviceName);
    fetch(restartUrl, getFetchOptions({ method: "POST" })).catch((error) => {
      // Ignore errors since device will restart
    });

    handleCloseModal();
    // Wait longer for device to restart (ESP32 typically takes 2-4 seconds)
    setTimeout(() => {
      window.location.reload();
    }, 5000);
  }, [config, handleCloseModal]);

  /**
   * Logout user and reload page
   */
  const handleLogout = useCallback(() => {
    clearAuthToken();
    handleCloseModal();
    // Reload page to trigger login screen
    window.location.reload();
  }, [handleCloseModal]);

  /**
   * Get action buttons
   * @returns {React.Element} Action buttons
   */
  const getActions = useCallback(() => {
    return (
      <>
        <IButton
          Icon={CloseIcon}
          onClick={handleCloseModal}
          tooltip={"Close Settings"}
        />
        <IButton
          Icon={RestartIcon}
          onClick={handleRestart}
          tooltip={"Restart Device"}
        />
        {config?.auth?.enabled && (
          <IButton
            Icon={LogoutIcon}
            onClick={handleLogout}
            tooltip={"Logout"}
          />
        )}
      </>
    );
  }, [config, handleRestart, handleLogout, handleCloseModal]);

  /**
   * Update JSON config when modal opens
   */
  useEffect(() => {
    if (open) {
      handleOpenModal();
      // Don't fetch device info here - only fetch when switching to Info tab
    }
  }, [open]);

  return (
    <SettingsModal
      open={open}
      onClose={handleCloseModal}
      title={
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: 1,
            width: "100%",
          }}
        >
          <SettingsIcon color="primary" />
          <span>System</span>
        </Box>
      }
      maxWidth={false}
      paperSx={{
        // Better desktop use of space; keep mobile behavior intact
        width: { xs: "90%", sm: "800px" },
        maxWidth: { xs: "90%", sm: "800px" },
        height: { xs: "90%", sm: "88vh" },
        maxHeight: { xs: "90%", sm: "88vh" },
      }}
      contentSx={{
        // Tighter padding so more fits on screen
        p: { xs: 2, sm: 2 },
        pt: { xs: 1.5, sm: 1.25 },
        pb: { xs: 3, sm: 2.5 }, // More bottom padding to prevent cutoff
        overflowY: "auto", // Enable scrolling for the content area
        overflowX: "hidden", // Prevent horizontal scroll
      }}
      actions={getActions()}
    >
      <DeviceSettingsInfoTab
        deviceInfo={deviceInfo}
        config={config}
        saveConfigToDevice={saveConfigToDeviceAndRefresh}
        infoLoading={infoLoading}
        infoError={infoError}
        mode={config?.wifi?.mode || "client"}
      />
    </SettingsModal>
  );
}
