import React, { useState, useEffect } from "react";
import { Tabs, Tab, Box } from "@mui/material";
import RestartIcon from "@mui/icons-material/RestartAlt";
import SettingsIcon from "@mui/icons-material/Settings";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";
import SaveButton from "./SaveButton";
import EditButton from "./EditButton";
import TabPanel from "./tabPanels/TabPanel";
import DeviceSettingsInfoTab from "./tabPanels/DeviceSettingsInfoTab";
import DeviceSettingsNetworkTab from "./tabPanels/DeviceSettingsNetworkTab";
import DeviceSettingsJsonTab from "./tabPanels/DeviceSettingsJsonTab";

export default function DeviceSettingsModal({
  config,
  saveConfig,
  saveConfigToDevice,
  open = false,
  onClose,
}) {
  const [activeTab, setActiveTab] = useState(0);

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

  // JSON editing state
  const [jsonConfig, setJsonConfig] = useState("");
  const [jsonError, setJsonError] = useState("");
  const [isEditable, setIsEditable] = useState(false);

  // Device info state
  const [deviceInfo, setDeviceInfo] = useState(null);
  const [infoLoading, setInfoLoading] = useState(false);
  const [infoError, setInfoError] = useState("");

  useEffect(() => {
    if (config) {
      setSsid(config.client?.ssid || "");
      setPassword(config.client?.password || "");
      setApSsid(config.ap?.ssid || "");
      setApPassword(config.ap?.password || "");
      setMode(config.mode || "client");
      setMdns(config.mdns || "");
    }
  }, [config]);

  // Function to update JSON config based on current network settings
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
    };

    // Remove apiURL for JSON display
    delete configToUpdate.apiURL;
    setJsonConfig(JSON.stringify(configToUpdate, null, 2));
  };

  // Update JSON config whenever network settings change
  useEffect(() => {
    if (config) {
      updateJsonConfig();
    }
  }, [ssid, password, apSsid, apPassword, mode, mdns, config]);

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
  const fetchDeviceInfo = async () => {
    if (!config?.apiURL) {
      console.warn("No apiURL configured in fetchDeviceInfo");
      return;
    }

    const fetchUrl = `${config.apiURL}/info`;

    setInfoLoading(true);
    setInfoError("");

    try {
      const response = await fetch(fetchUrl);
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      const data = await response.json();
      console.log("Fetched device info:", data);
      setDeviceInfo(data);
    } catch (error) {
      console.error("Failed to fetch device info:", error);
      setInfoError(error.message);
      setDeviceInfo(null); // Ensure deviceInfo is set to null on error
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
    setActiveTab(newValue);
    // Fetch device info when switching to Info tab
    if (newValue === 0) {
      fetchDeviceInfo();
    }
  };

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

  const handleRestart = () => {
    fetch(`${config.apiURL}/restart`, {
      method: "GET",
    }).catch((error) => {
      // Ignore errors since device will restart
    });

    handleCloseModal();
    setTimeout(() => {
      window.location.reload();
    }, 1000);
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
    if (activeTab === 0) {
      // Info tab - only show restart button
      return (
        <>
          <IButton
            Icon={RestartIcon}
            onClick={handleRestart}
            tooltip={"Restart Device"}
          />
        </>
      );
    } else if (activeTab === 1) {
      // Network settings tab
      return (
        <>
          <IButton
            Icon={RestartIcon}
            onClick={handleRestart}
            tooltip={"Restart Device"}
          />
          <SaveButton
            onClick={handleNetworkSave}
            tooltip="Save Settings to Device"
          />
        </>
      );
    } else {
      // JSON editing tab
      return (
        <>
          <IButton
            Icon={RestartIcon}
            onClick={handleRestart}
            tooltip={"Restart Device"}
          />
          <EditButton
            onClick={toggleEditability}
            tooltip={isEditable ? "Stop Editing" : "Edit"}
            isEditing={isEditable}
          />
          <SaveButton
            onClick={handleJsonSave}
            tooltip="Save Configuration to Device"
          />
        </>
      );
    }
  };

  // Update JSON config when modal opens
  React.useEffect(() => {
    if (open) {
      handleOpenModal();
      // Fetch device info since Info tab is default
      fetchDeviceInfo();
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
          sx={{
            "& .MuiTab-root": {
              color: "primary.main",
              "&.Mui-selected": {
                color: "primary.main",
              },
            },
          }}
        >
          <Tab label="Info" />
          <Tab label="Network" />
          <Tab label="JSON" />
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
        <DeviceSettingsJsonTab
          jsonConfig={jsonConfig}
          setJsonConfig={setJsonConfig}
          jsonError={jsonError}
          setJsonError={setJsonError}
          isEditable={isEditable}
        />
      </TabPanel>
    </SettingsModal>
  );
}
