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
import DeviceSettingsInfoTab from "./tabPanels/DeviceSettingsInfoTab";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";
import { clearAuthToken } from "../utils/authUtils";
import { getUserFriendlyErrorMessage, logError } from "../utils/errorUtils";

export default function DeviceSettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
}) {
  // Device info state
  const [deviceInfo, setDeviceInfo] = useState(null);
  const [infoLoading, setInfoLoading] = useState(false);
  const [infoError, setInfoError] = useState("");
  const [retryCount, setRetryCount] = useState(0);

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
    fetchDeviceInfo();
  }, []);

  /**
   * Handle modal close
   */
  const handleCloseModal = useCallback(() => {
    if (onClose) onClose();
  }, [onClose]);

  /**
   * Fetch device information from /api/info endpoint
   * Includes automatic retry logic for transient failures
   * @param {boolean} isRetry - Whether this is a retry attempt
   */
  const fetchDeviceInfo = async (isRetry = false) => {
    const fetchUrl = buildApiUrl("/api/info");
    setInfoLoading(true);
    if (!isRetry) {
      setInfoError("");
    }

    // Add timeout handling similar to main config fetch
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 6000); // 6 second timeout

    try {
      const response = await fetch(
        fetchUrl,
        getFetchOptions({
          signal: controller.signal,
        })
      );

      clearTimeout(timeoutId);

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      const data = await response.json();
      setDeviceInfo(data);
      setRetryCount(0); // Reset retry count on success
    } catch (error) {
      clearTimeout(timeoutId);
      const errorMessage = getUserFriendlyErrorMessage(
        error,
        "fetching device info"
      );
      setInfoError(errorMessage);
      logError(error, "Device Info Fetch");
      setDeviceInfo(null);

      // Auto-retry once after a short delay
      if (!isRetry && retryCount < 1) {
        setTimeout(() => {
          setRetryCount((prev) => prev + 1);
          fetchDeviceInfo(true);
        }, 2000);
      }
    } finally {
      setInfoLoading(false);
    }
  };

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
        saveConfigToDevice={saveConfigToDevice}
        infoLoading={infoLoading}
        infoError={infoError}
        mode={config?.wifi?.mode || "client"}
      />
    </SettingsModal>
  );
}
