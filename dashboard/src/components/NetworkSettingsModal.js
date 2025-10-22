import React, { useState, useEffect } from "react";
import {
  TextField,
  FormControl,
  FormControlLabel,
  Switch,
  Tabs,
  Tab,
  Box,
  Alert,
  InputAdornment,
  IconButton,
} from "@mui/material";
import RestartIcon from "@mui/icons-material/RestartAlt";
import SaveIcon from "@mui/icons-material/SaveAs";
import SettingsIcon from "@mui/icons-material/Settings";
import EditIcon from "@mui/icons-material/Edit";
import VisibilityIcon from "@mui/icons-material/Visibility";
import VisibilityOffIcon from "@mui/icons-material/VisibilityOff";
import IButton from "./IButton";
import SettingsModal from "./SettingsModal";

// Tab Panel component
function TabPanel({ children, value, index, ...other }) {
  return (
    <div
      role="tabpanel"
      hidden={value !== index}
      id={`settings-tabpanel-${index}`}
      aria-labelledby={`settings-tab-${index}`}
      {...other}
    >
      {value === index && <Box sx={{ pt: 3 }}>{children}</Box>}
    </div>
  );
}

export default function NetworkSettingsModal({
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
  };

  const handleCloseModal = () => {
    setJsonError("");
    setIsEditable(false);
    if (onClose) onClose();
  };

  // Function to update JSON config based on current network settings
  const updateJsonConfig = () => {
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

  const handleTabChange = (event, newValue) => {
    setActiveTab(newValue);
  };

  // Network settings handlers
  const handleSsidChange = (event) => {
    setSsid(event.target.value);
  };

  const handlePasswordChange = (event) => {
    setPassword(event.target.value);
  };

  const handleApSsidChange = (event) => {
    setApSsid(event.target.value);
  };

  const handleApPasswordChange = (event) => {
    setApPassword(event.target.value);
  };

  const handleModeToggle = (event) => {
    setMode(event.target.checked ? "client" : "ap");
  };

  const handleMDNSChange = (event) => {
    setMdns(event.target.value);
  };

  const handleTogglePasswordVisibility = () => {
    setShowPassword((prev) => !prev);
  };

  const handleToggleApPasswordVisibility = () => {
    setShowApPassword((prev) => !prev);
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

  const handleJsonChange = (event) => {
    setJsonConfig(event.target.value);
    if (jsonError) {
      setJsonError("");
    }
  };

  const toggleEditability = () => {
    setIsEditable((prev) => !prev);
  };

  // Determine which actions to show based on active tab
  const getActions = () => {
    if (activeTab === 0) {
      // Network settings tab
      return (
        <>
          <IButton
            Icon={RestartIcon}
            onClick={handleRestart}
            tooltip={"Restart Device"}
          />
          <IButton
            color="primary"
            Icon={SaveIcon}
            onClick={handleNetworkSave}
            tooltip={"Save Settings to Device"}
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
          <IButton
            color="primary"
            Icon={SaveIcon}
            onClick={handleJsonSave}
            tooltip="Save Configuration to Device"
          />
          <IButton
            color={isEditable ? "secondary" : "default"}
            Icon={EditIcon}
            onClick={toggleEditability}
            tooltip={isEditable ? "Stop Editing" : "Edit"}
          />
        </>
      );
    }
  };

  // Update JSON config when modal opens
  React.useEffect(() => {
    if (open) {
      handleOpenModal();
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
          Settings
        </span>
      }
      maxWidth="lg"
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
          <Tab label="Network" />
          <Tab label="JSON" />
        </Tabs>
      </Box>

      <TabPanel value={activeTab} index={0}>
        <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
          <TextField
            label="mDNS Hostname"
            value={mdns}
            onChange={handleMDNSChange}
            variant="outlined"
            fullWidth
          />
        </FormControl>
        <FormControl variant="outlined" sx={{ marginTop: 1 }}>
          <FormControlLabel
            control={
              <Switch checked={mode === "client"} onChange={handleModeToggle} />
            }
            label={mode === "client" ? "WiFi Client" : "Access Point"}
          />
        </FormControl>
        {mode === "client" ? (
          <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
            <TextField
              label="SSID"
              value={ssid}
              onChange={handleSsidChange}
              variant="outlined"
              fullWidth
            />
            <TextField
              type={showPassword ? "text" : "password"}
              label="Password"
              value={password}
              onChange={handlePasswordChange}
              variant="outlined"
              fullWidth
              sx={{ marginTop: 1 }}
              InputProps={{
                endAdornment: (
                  <InputAdornment position="end">
                    <IconButton
                      onClick={handleTogglePasswordVisibility}
                      edge="end"
                    >
                      {showPassword ? (
                        <VisibilityOffIcon />
                      ) : (
                        <VisibilityIcon />
                      )}
                    </IconButton>
                  </InputAdornment>
                ),
              }}
            />
          </FormControl>
        ) : (
          <FormControl fullWidth variant="outlined" sx={{ marginTop: 1 }}>
            <TextField
              label="SSID"
              value={apSsid}
              onChange={handleApSsidChange}
              variant="outlined"
              fullWidth
            />
            <TextField
              type={showApPassword ? "text" : "password"}
              label="Password"
              value={apPassword}
              onChange={handleApPasswordChange}
              variant="outlined"
              fullWidth
              sx={{ marginTop: 1 }}
              InputProps={{
                endAdornment: (
                  <InputAdornment position="end">
                    <IconButton
                      onClick={handleToggleApPasswordVisibility}
                      edge="end"
                    >
                      {showApPassword ? (
                        <VisibilityOffIcon />
                      ) : (
                        <VisibilityIcon />
                      )}
                    </IconButton>
                  </InputAdornment>
                ),
              }}
            />
          </FormControl>
        )}
      </TabPanel>

      <TabPanel value={activeTab} index={1}>
        {jsonError && (
          <Alert severity="error" sx={{ marginBottom: 2 }}>
            {jsonError}
          </Alert>
        )}
        <TextField
          label="Configuration JSON"
          value={jsonConfig}
          onChange={handleJsonChange}
          variant="outlined"
          fullWidth
          multiline
          rows={20}
          error={!!jsonError}
          disabled={!isEditable}
          sx={{
            marginTop: 2,
            "& .MuiInputBase-input": {
              fontFamily: "monospace",
              fontSize: "0.875rem",
            },
          }}
        />
      </TabPanel>
    </SettingsModal>
  );
}
