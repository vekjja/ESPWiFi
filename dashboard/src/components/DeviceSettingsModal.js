/**
 * Device Settings Modal Component
 * Manages device configuration including network, auth, OTA, and device info
 * Organized into tabs for better UX and maintainability
 */

import React, { useState, useEffect, useCallback } from "react";
import { Tabs, Tab, Box, useTheme, useMediaQuery } from "@mui/material";
import RestartIcon from "@mui/icons-material/RestartAlt";
import LogoutIcon from "@mui/icons-material/Logout";
import SettingsIcon from "@mui/icons-material/Settings";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";
import SaveButton from "./SaveButton";
import EditButton from "./EditButton";
import TabPanel from "./tabPanels/TabPanel";
import DeviceSettingsInfoTab from "./tabPanels/DeviceSettingsInfoTab";
import DeviceSettingsNetworkTab from "./tabPanels/DeviceSettingsNetworkTab";
import DeviceSettingsAuthTab from "./tabPanels/DeviceSettingsAuthTab";
import DeviceSettingsJsonTab from "./tabPanels/DeviceSettingsJsonTab";
import DeviceSettingsOTATab from "./tabPanels/DeviceSettingsOTATab";
import { buildApiUrl, getFetchOptions } from "../utils/apiUtils";
import { clearAuthToken } from "../utils/authUtils";
import { getUserFriendlyErrorMessage, logError } from "../utils/errorUtils";

/**
 * Tab indices for easier maintenance
 */
const TAB_INFO = 0;
const TAB_NETWORK = 1;
const TAB_AUTH = 2;
const TAB_JSON = 3;
const TAB_OTA = 4;

export default function DeviceSettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
}) {
  const theme = useTheme();
  const isMobile = useMediaQuery(theme.breakpoints.down("sm"));
  const [activeTab, setActiveTab] = useState(0);

  // Check if OTA is enabled (default to true if not explicitly set to false)
  const otaEnabled = config?.ota?.enabled !== false;

  // Network settings state
  const [ssid, setSsid] = useState("");
  const [deviceName, setDeviceName] = useState(config?.["deviceName"] || "");
  const [password, setPassword] = useState("");
  const [apSsid, setApSsid] = useState("");
  const [apPassword, setApPassword] = useState("");
  const [mode, setMode] = useState("client");
  const [txPower, setTxPower] = useState(19.5);
  const [powerSave, setPowerSave] = useState("none");

  // Password visibility state
  const [showPassword, setShowPassword] = useState(false);
  const [showApPassword, setShowApPassword] = useState(false);
  const [showAuthPassword, setShowAuthPassword] = useState(false);

  // Auth settings state
  const [authEnabled, setAuthEnabled] = useState(false);
  const [authUsername, setAuthUsername] = useState("");
  const [authPassword, setAuthPassword] = useState("");

  // JSON editing state
  const [jsonConfig, setJsonConfig] = useState("");
  const [jsonError, setJsonError] = useState("");
  const [isEditable, setIsEditable] = useState(false);

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
      setSsid(config.wifi?.client?.ssid || "");
      setPassword(config.wifi?.client?.password || "");
      setApSsid(config.wifi?.accessPoint?.ssid || "");
      setApPassword(config.wifi?.accessPoint?.password || "");
      // Handle backward compatibility: "ap" mode is now "accessPoint"
      const wifiMode = config.wifi?.mode || "client";
      setMode(wifiMode === "ap" ? "accessPoint" : wifiMode);
      setDeviceName(config.deviceName || "");
      setAuthEnabled(config.auth?.enabled ?? false);
      setAuthUsername(config.auth?.username || "");
      setAuthPassword(config.auth?.password || "");
      setTxPower(config.wifi?.power?.txPower ?? 19.5);
      setPowerSave(config.wifi?.power?.powerSave || "none");
    }
  }, [config]);

  /**
   * Update JSON config display based on current form values
   * Syncs all tabs to the JSON view
   */
  const updateJsonConfig = useCallback(() => {
    if (!config) return;
    const configToUpdate = {
      ...config,
      deviceName: deviceName,
      wifi: {
        ...config.wifi,
        mode: mode,
        client: {
          ssid: ssid,
          password: password,
        },
        accessPoint: {
          ssid: apSsid,
          password: apPassword,
        },
        power: {
          txPower: txPower,
          powerSave: powerSave,
        },
      },
      auth: {
        ...config.auth,
        enabled: authEnabled,
        username: authUsername,
        password: authPassword,
      },
    };

    // Remove apiURL for JSON display
    delete configToUpdate.apiURL;
    setJsonConfig(JSON.stringify(configToUpdate, null, 2));
  }, [
    config,
    deviceName,
    ssid,
    password,
    apSsid,
    apPassword,
    mode,
    authEnabled,
    authUsername,
    authPassword,
    txPower,
    powerSave,
  ]);

  /**
   * Update JSON config whenever form values change
   */
  useEffect(() => {
    if (config) {
      updateJsonConfig();
    }
  }, [config, updateJsonConfig]);

  /**
   * Handle modal open - initialize JSON config
   */
  const handleOpenModal = useCallback(() => {
    // Format the config as pretty JSON when opening the modal, excluding apiURL
    const configWithoutAPI = { ...config };
    delete configWithoutAPI.apiURL;
    setJsonConfig(JSON.stringify(configWithoutAPI, null, 2));
    setJsonError("");
    setIsEditable(false);
    setActiveTab(TAB_INFO);
  }, [config]);

  /**
   * Handle modal close - reset state
   */
  const handleCloseModal = useCallback(() => {
    setJsonError("");
    setIsEditable(false);
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
   * Handle tab change with validation
   */
  const handleTabChange = (event, newValue) => {
    // Prevent switching to OTA tab if OTA is disabled
    if (newValue === TAB_OTA && !otaEnabled) return;

    setActiveTab(newValue);

    // Fetch device info when switching to Info tab
    if (newValue === TAB_INFO) {
      fetchDeviceInfo();
    }
  };

  /**
   * Reset activeTab if OTA is disabled and we're on the OTA tab
   */
  useEffect(() => {
    if (activeTab === TAB_OTA && !otaEnabled) {
      setActiveTab(TAB_INFO);
    }
  }, [otaEnabled, activeTab]);

  /**
   * Fetch device info when Info tab is first rendered
   */
  useEffect(() => {
    if (activeTab === TAB_INFO && !deviceInfo && !infoLoading && !infoError) {
      fetchDeviceInfo();
    }
  }, [activeTab, deviceInfo, infoLoading, infoError]);

  /**
   * Validate hostname according to RFC 1123
   * @param {string} hostname - Hostname to validate
   * @returns {boolean} True if valid
   */
  const isValidHostname = (hostname) => {
    const regex = /^(?!-)[A-Za-z0-9-]{1,63}(?<!-)$/;
    return regex.test(hostname);
  };

  /**
   * Save network configuration changes
   */
  const handleNetworkSave = useCallback(() => {
    if (!isValidHostname(deviceName)) {
      alert("Invalid device name. Please enter a valid hostname.");
      return;
    }

    const configToSave = {
      ...config,
      deviceName: deviceName,
      wifi: {
        ...config.wifi,
        mode: mode,
        client: {
          ssid: ssid,
          password: password,
        },
        accessPoint: {
          ssid: apSsid,
          password: apPassword,
        },
        power: {
          txPower: txPower,
          powerSave: powerSave,
        },
      },
    };

    // Save to device (not just local config)
    saveConfigToDevice(configToSave);
    handleCloseModal();
  }, [
    deviceName,
    config,
    ssid,
    password,
    apSsid,
    apPassword,
    mode,
    txPower,
    powerSave,
    saveConfigToDevice,
    handleCloseModal,
  ]);

  /**
   * Save authentication configuration changes
   */
  const handleAuthSave = useCallback(() => {
    const configToSave = {
      ...config,
      auth: {
        ...config.auth,
        enabled: authEnabled,
        username: authUsername,
        password: authPassword,
      },
    };

    // Save to device (not just local config)
    saveConfigToDevice(configToSave);
    handleCloseModal();
  }, [
    config,
    authEnabled,
    authUsername,
    authPassword,
    saveConfigToDevice,
    handleCloseModal,
  ]);

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
   * Save JSON configuration
   */
  const handleJsonSave = useCallback(() => {
    try {
      const parsedConfig = JSON.parse(jsonConfig);

      if (!parsedConfig.deviceName) {
        setJsonError("Configuration must include 'deviceName' field");
        return;
      }

      delete parsedConfig.apiURL;
      setJsonError("");
      saveConfigToDevice(parsedConfig);
      handleCloseModal();
    } catch (error) {
      setJsonError("Invalid JSON format. Please check your configuration.");
    }
  }, [jsonConfig, saveConfigToDevice, handleCloseModal]);

  /**
   * Toggle JSON edit mode
   */
  const toggleEditability = useCallback(() => {
    setIsEditable((prev) => !prev);
  }, []);

  /**
   * Get action buttons based on active tab
   * @returns {React.Element} Action buttons for current tab
   */
  const getActions = useCallback(() => {
    // Common buttons (restart and logout) - logout only shown when auth is enabled
    const commonButtons = (
      <>
        <IButton
          Icon={RestartIcon}
          onClick={handleRestart}
          tooltip={"Restart Device"}
        />
        {authEnabled && (
          <IButton
            Icon={LogoutIcon}
            onClick={handleLogout}
            tooltip={"Logout"}
          />
        )}
      </>
    );

    if (activeTab === TAB_INFO) {
      return commonButtons;
    } else if (activeTab === TAB_NETWORK) {
      return (
        <>
          <SaveButton
            onClick={handleNetworkSave}
            tooltip="Save Settings to Device"
          />
          {commonButtons}
        </>
      );
    } else if (activeTab === TAB_AUTH) {
      return (
        <>
          <SaveButton
            onClick={handleAuthSave}
            tooltip="Save Settings to Device"
          />
          {commonButtons}
        </>
      );
    } else if (activeTab === TAB_JSON) {
      return (
        <>
          <EditButton
            onClick={toggleEditability}
            tooltip={isEditable ? "Stop Editing" : "Edit"}
            isEditing={isEditable}
          />
          <SaveButton
            onClick={handleJsonSave}
            tooltip="Save Configuration to Device"
          />
          {commonButtons}
        </>
      );
    } else {
      return commonButtons;
    }
  }, [
    activeTab,
    authEnabled,
    handleNetworkSave,
    handleAuthSave,
    handleJsonSave,
    toggleEditability,
    isEditable,
    handleRestart,
    handleLogout,
  ]);

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
        width: { xs: "90%", sm: "63vw" },
        maxWidth: { xs: "90%", sm: "63vw" },
        height: { xs: "90%", sm: "88vh" },
        maxHeight: { xs: "90%", sm: "88vh" },
      }}
      contentSx={{
        // Tighter padding so more fits on screen
        p: { xs: 2, sm: 1.5 },
        pt: { xs: 1.5, sm: 1.25 },
        pb: { xs: 1.5, sm: 1.25 },
      }}
      actions={getActions()}
      tabs={
        <Tabs
          value={activeTab}
          onChange={handleTabChange}
          aria-label="settings tabs"
          variant={isMobile ? "scrollable" : "standard"}
          scrollButtons="auto"
          centered={!isMobile}
          sx={{
            "& .MuiTab-root": {
              color: "primary.main",
              "&.Mui-selected": {
                color: "primary.main",
              },
              minWidth: isMobile ? "auto" : undefined,
              padding: isMobile ? "12px 16px" : undefined,
            },
          }}
        >
          <Tab label="Info" />
          <Tab label="WiFi" />
          <Tab label="Auth" />
          <Tab label="JSON" />
          {otaEnabled && <Tab label="Update" />}
        </Tabs>
      }
    >
      <Box
        sx={{
          display: "flex",
          flexDirection: "column",
          flex: 1,
          minHeight: 0,
        }}
      >
        <TabPanel value={activeTab} index={0}>
          <DeviceSettingsInfoTab
            deviceInfo={deviceInfo}
            infoLoading={infoLoading}
            infoError={infoError}
            mode={mode}
          />
        </TabPanel>

        <TabPanel value={activeTab} index={1}>
          <DeviceSettingsNetworkTab
            ssid={ssid}
            setSsid={setSsid}
            password={password}
            setPassword={setPassword}
            apSsid={apSsid}
            setApSsid={setApSsid}
            apPassword={apPassword}
            setApPassword={setApPassword}
            mode={mode}
            setMode={setMode}
            deviceName={deviceName}
            setDeviceName={setDeviceName}
            showPassword={showPassword}
            setShowPassword={setShowPassword}
            showApPassword={showApPassword}
            setShowApPassword={setShowApPassword}
            txPower={txPower}
            setTxPower={setTxPower}
            powerSave={powerSave}
            setPowerSave={setPowerSave}
          />
        </TabPanel>

        <TabPanel value={activeTab} index={2}>
          <DeviceSettingsAuthTab
            authEnabled={authEnabled}
            setAuthEnabled={setAuthEnabled}
            username={authUsername}
            setUsername={setAuthUsername}
            password={authPassword}
            setPassword={setAuthPassword}
            showPassword={showAuthPassword}
            setShowPassword={setShowAuthPassword}
          />
        </TabPanel>

        <TabPanel value={activeTab} index={3}>
          <DeviceSettingsJsonTab
            jsonConfig={jsonConfig}
            setJsonConfig={setJsonConfig}
            jsonError={jsonError}
            setJsonError={setJsonError}
            isEditable={isEditable}
            isMobile={isMobile}
          />
        </TabPanel>

        {otaEnabled && (
          <TabPanel value={activeTab} index={4}>
            <DeviceSettingsOTATab config={config} />
          </TabPanel>
        )}
      </Box>
    </SettingsModal>
  );
}
