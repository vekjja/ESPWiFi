import React, { useState, useEffect } from "react";
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
  const [mdns, setMdns] = useState(config?.["mdns"] || "");
  const [password, setPassword] = useState("");
  const [apSsid, setApSsid] = useState("");
  const [apPassword, setApPassword] = useState("");
  const [mode, setMode] = useState("client");

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

  useEffect(() => {
    if (config) {
      setSsid(config.client?.ssid || "");
      setPassword(config.client?.password || "");
      setApSsid(config.ap?.ssid || "");
      setApPassword(config.ap?.password || "");
      setMode(config.mode || "client");
      setMdns(config.mdns || "");
      setAuthEnabled(config.auth?.enabled ?? false);
      setAuthUsername(config.auth?.username || "");
      setAuthPassword(config.auth?.password || "");
    }
  }, [config]);

  // Function to update JSON config based on current network and auth settings
  const updateJsonConfig = () => {
    if (!config) return;
    const configToUpdate = {
      ...config,
      mode: mode,
      mdns: mdns,
      client: {
        ssid: ssid,
        password: password,
      },
      ap: {
        ssid: apSsid,
        password: apPassword,
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
  };

  // Update JSON config whenever network or auth settings change
  useEffect(() => {
    if (config) {
      updateJsonConfig();
    }
  }, [
    ssid,
    password,
    apSsid,
    apPassword,
    mode,
    mdns,
    authEnabled,
    authUsername,
    authPassword,
    config,
  ]);

  const handleOpenModal = () => {
    // Format the config as pretty JSON when opening the modal, excluding apiURL
    const configWithoutAPI = { ...config };
    delete configWithoutAPI.apiURL;
    setJsonConfig(JSON.stringify(configWithoutAPI, null, 2));
    setJsonError("");
    setIsEditable(false);
    setActiveTab(0);
    // Don't fetch here - fetch only when switching to Info tab
  };

  // Fetch device info from /info endpoint
  const fetchDeviceInfo = async (isRetry = false) => {
    const fetchUrl = buildApiUrl("/info");
    console.log(
      "Fetching device info from:",
      fetchUrl,
      isRetry ? `(retry ${retryCount + 1})` : ""
    );

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
      console.log("Fetched device info:", data);
      setDeviceInfo(data);
      setRetryCount(0); // Reset retry count on success
    } catch (error) {
      clearTimeout(timeoutId);
      if (error.name === "AbortError") {
        console.error("Device info fetch timed out");
        setInfoError("Request timed out - device may be offline");
      } else {
        console.error("Failed to fetch device info:", error);
        setInfoError(`Failed to fetch device info: ${error.message}`);
      }
      setDeviceInfo(null); // Ensure deviceInfo is set to null on error

      // Auto-retry once after a short delay
      if (!isRetry && retryCount < 1) {
        console.log("Retrying device info fetch in 2 seconds...");
        setTimeout(() => {
          setRetryCount((prev) => prev + 1);
          fetchDeviceInfo(true);
        }, 2000);
      }
    } finally {
      setInfoLoading(false);
    }
  };

  const handleCloseModal = () => {
    setJsonError("");
    setIsEditable(false);
    if (onClose) onClose();
  };

  const handleTabChange = (event, newValue) => {
    // Prevent switching to OTA tab if OTA is disabled
    if (newValue === 4 && !otaEnabled) {
      return;
    }
    setActiveTab(newValue);
    // Fetch device info when switching to Info tab
    if (newValue === 0) {
      fetchDeviceInfo();
    }
  };

  // Reset activeTab if OTA is disabled and we're on the OTA tab
  useEffect(() => {
    if (activeTab === 4 && !otaEnabled) {
      setActiveTab(0);
    }
  }, [otaEnabled, activeTab]);

  // Fetch device info when Info tab is first rendered
  React.useEffect(() => {
    if (activeTab === 0 && !deviceInfo && !infoLoading && !infoError) {
      fetchDeviceInfo();
    }
  }, [activeTab, deviceInfo, infoLoading, infoError]);

  const isValidHostname = (hostname) => {
    const regex = /^(?!-)[A-Za-z0-9-]{1,63}(?<!-)$/;
    return regex.test(hostname);
  };

  const handleNetworkSave = () => {
    if (!isValidHostname(mdns)) {
      alert("Invalid mDNS hostname. Please enter a valid hostname.");
      return;
    }

    const configToSave = {
      ...config,
      mode: mode,
      mdns: mdns,
      client: {
        ssid: ssid,
        password: password,
      },
      ap: {
        ssid: apSsid,
        password: apPassword,
      },
    };

    // Save to device (not just local config)
    saveConfigToDevice(configToSave);
    handleCloseModal();
  };

  const handleAuthSave = () => {
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
  };

  const handleRestart = () => {
    const restartUrl = buildApiUrl("/restart");
    fetch(restartUrl, getFetchOptions({ method: "GET" })).catch((error) => {
      // Ignore errors since device will restart
    });

    handleCloseModal();
    setTimeout(() => {
      window.location.reload();
    }, 1000);
  };

  const handleLogout = () => {
    clearAuthToken();
    handleCloseModal();
    // Reload page to trigger login screen
    window.location.reload();
  };

  // JSON editing handlers
  const handleJsonSave = () => {
    try {
      const parsedConfig = JSON.parse(jsonConfig);

      if (!parsedConfig.mdns) {
        setJsonError("Configuration must include 'mdns' field");
        return;
      }

      delete parsedConfig.apiURL;
      setJsonError("");
      saveConfigToDevice(parsedConfig);
      handleCloseModal();
    } catch (error) {
      setJsonError("Invalid JSON format. Please check your configuration.");
    }
  };

  const toggleEditability = () => {
    setIsEditable((prev) => !prev);
  };

  // Determine which actions to show based on active tab
  const getActions = () => {
    // Common buttons (restart and logout) - always shown on the right
    const commonButtons = (
      <>
        <IButton
          Icon={RestartIcon}
          onClick={handleRestart}
          tooltip={"Restart Device"}
        />
        <IButton Icon={LogoutIcon} onClick={handleLogout} tooltip={"Logout"} />
      </>
    );

    if (activeTab === 0) {
      // Info tab - only show restart and logout buttons
      return commonButtons;
    } else if (activeTab === 1) {
      // Network settings tab - Save button on left, restart/logout on right
      return (
        <>
          <SaveButton
            onClick={handleNetworkSave}
            tooltip="Save Settings to Device"
          />
          {commonButtons}
        </>
      );
    } else if (activeTab === 2) {
      // Auth settings tab - Save button on left, restart/logout on right
      return (
        <>
          <SaveButton
            onClick={handleAuthSave}
            tooltip="Save Settings to Device"
          />
          {commonButtons}
        </>
      );
    } else if (activeTab === 3) {
      // JSON editing tab - Edit and Save on left, restart/logout on right
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
      // OTA tab - only show restart and logout buttons
      return commonButtons;
    }
  };

  // Update JSON config when modal opens
  React.useEffect(() => {
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
        <span
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            gap: "8px",
            width: "100%",
          }}
        >
          <SettingsIcon color="primary" />
          Device Settings
        </span>
      }
      maxWidth={false}
      actions={getActions()}
    >
      <Box sx={{ borderBottom: 1, borderColor: "divider" }}>
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
          <Tab label="Network" />
          <Tab label="Auth" />
          <Tab label="JSON" />
          {otaEnabled && <Tab label="Updates" />}
        </Tabs>
      </Box>

      <TabPanel value={activeTab} index={0}>
        <DeviceSettingsInfoTab
          deviceInfo={deviceInfo}
          infoLoading={infoLoading}
          infoError={infoError}
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
          mdns={mdns}
          setMdns={setMdns}
          showPassword={showPassword}
          setShowPassword={setShowPassword}
          showApPassword={showApPassword}
          setShowApPassword={setShowApPassword}
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
        />
      </TabPanel>

      {otaEnabled && (
        <TabPanel value={activeTab} index={4}>
          <DeviceSettingsOTATab config={config} />
        </TabPanel>
      )}
    </SettingsModal>
  );
}
