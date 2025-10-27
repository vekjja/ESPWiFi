import React, { useState, useEffect, useRef } from "react";
import {
  Box,
  Alert,
  Typography,
  Card,
  CardContent,
  Grid,
  Button,
  LinearProgress,
  Skeleton,
} from "@mui/material";
import CloudUploadIcon from "@mui/icons-material/CloudUpload";
import WebIcon from "@mui/icons-material/Web";
import CheckCircleIcon from "@mui/icons-material/CheckCircle";
import ErrorIcon from "@mui/icons-material/Error";
import InfoIcon from "@mui/icons-material/Info";
import { buildApiUrl } from "../../utils/apiUtils";
import { bytesToHumanReadable } from "../../utils/formatUtils";

export default function DeviceSettingsOTATab({ config }) {
  const [otaStatus, setOtaStatus] = useState(null);
  const [otaLoading, setOtaLoading] = useState(false);
  const [otaError, setOtaError] = useState("");
  const [firmwareFile, setFirmwareFile] = useState(null);
  const [filesystemFiles, setFilesystemFiles] = useState([]);

  // Firmware upload state
  const [firmwareUploadProgress, setFirmwareUploadProgress] = useState(0);
  const [firmwareUploadStatus, setFirmwareUploadStatus] = useState("");

  // Dashboard upload state
  const [dashboardUploadProgress, setDashboardUploadProgress] = useState(0);
  const [dashboardUploadStatus, setDashboardUploadStatus] = useState("");

  // Shared state
  const [otaProgress, setOtaProgress] = useState(0);
  const [isUploading, setIsUploading] = useState(false);
  const [progressPolling, setProgressPolling] = useState(null);

  const firmwareInputRef = useRef(null);
  const filesystemInputRef = useRef(null);

  // Fetch OTA status information
  const fetchOTAStatus = async () => {
    const fetchUrl = buildApiUrl("/api/ota/status", config?.mdns);
    console.log("Fetching OTA status from:", fetchUrl);

    setOtaLoading(true);
    setOtaError("");

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 6000);

    try {
      const response = await fetch(fetchUrl, {
        signal: controller.signal,
      });

      clearTimeout(timeoutId);

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }
      const data = await response.json();
      console.log("Fetched OTA status:", data);
      setOtaStatus(data);
    } catch (error) {
      clearTimeout(timeoutId);
      if (error.name === "AbortError") {
        console.error("OTA status fetch timed out");
        setOtaError("Request timed out - device may be offline");
      } else {
        console.error("Failed to fetch OTA status:", error);
        setOtaError(`Failed to fetch OTA status: ${error.message}`);
      }
      setOtaStatus(null);
    } finally {
      setOtaLoading(false);
    }
  };

  // Poll for OTA progress
  const startProgressPolling = () => {
    if (progressPolling) return; // Already polling

    const pollInterval = setInterval(async () => {
      try {
        const fetchUrl = buildApiUrl("/api/ota/progress", config?.mdns);
        const response = await fetch(fetchUrl);
        if (response.ok) {
          const data = await response.json();
          if (data.in_progress && data.progress !== undefined) {
            setOtaProgress(data.progress);
          } else if (!data.in_progress) {
            // OTA not in progress, stop polling
            clearInterval(progressPolling);
            setProgressPolling(null);
          }
        }
      } catch (error) {
        // Connection lost - device might be restarting
        clearInterval(progressPolling);
        setProgressPolling(null);

        if (firmwareUploadProgress === 100) {
          setFirmwareUploadStatus("Device is restarting... Please wait.");
          // Start checking for device to come back online
          setTimeout(() => {
            checkDeviceStatus();
          }, 3000);
        }
      }
    }, 1000); // Poll every second

    setProgressPolling(pollInterval);
  };

  // Check if device is back online after restart
  const checkDeviceStatus = async () => {
    try {
      const fetchUrl = buildApiUrl("/api/ota/status", config?.mdns);
      const response = await fetch(fetchUrl);
      if (response.ok) {
        setFirmwareUploadStatus(
          "Device is back online! Update completed successfully."
        );
        setOtaProgress(100);
        fetchOTAStatus(); // Refresh status
      }
    } catch (error) {
      // Device still restarting, try again in a few seconds
      setTimeout(() => {
        checkDeviceStatus();
      }, 2000);
    }
  };

  // Handle file selection
  const handleFileSelect = (event, type) => {
    if (type === "firmware") {
      const file = event.target.files[0];
      setFirmwareFile(file);
      // Automatically start firmware upload when file is selected
      if (file) {
        uploadFirmware(file);
      }
    } else {
      // Handle multiple files for filesystem upload
      const files = Array.from(event.target.files);
      setFilesystemFiles(files);
      // Automatically start dashboard upload when files are selected
      if (files.length > 0) {
        uploadFilesystemFiles(files);
      }
    }
  };

  // Upload firmware
  const uploadFirmware = async (file) => {
    if (!file) return;

    setIsUploading(true);
    setFirmwareUploadProgress(0);
    setOtaProgress(0);
    setFirmwareUploadStatus("Initializing firmware update...");

    try {
      // Step 1: Start OTA update
      const startUrl = buildApiUrl(
        "/api/ota/start?mode=firmware",
        config?.mdns
      );
      console.log("Starting OTA with URL:", startUrl);
      const startResponse = await fetch(startUrl, { method: "POST" });
      console.log(
        "OTA start response:",
        startResponse.status,
        startResponse.statusText
      );

      if (!startResponse.ok) {
        let errorMessage = "Failed to start OTA";
        try {
          const errorData = await startResponse.json();
          errorMessage = errorData.error || errorMessage;
        } catch (e) {
          errorMessage = `HTTP ${startResponse.status}: ${startResponse.statusText}`;
        }
        console.error("OTA start failed:", errorMessage);
        throw new Error(errorMessage);
      }

      // Step 2: Upload firmware file
      setFirmwareUploadStatus("Uploading firmware...");
      const formData = new FormData();
      formData.append("file", file);

      const xhr = new XMLHttpRequest();

      // Track upload progress
      xhr.upload.addEventListener("progress", (e) => {
        if (e.lengthComputable) {
          const percentComplete = (e.loaded / e.total) * 100;
          setFirmwareUploadProgress(percentComplete);
        }
      });

      // Start polling for server-side progress when upload starts
      xhr.upload.addEventListener("loadstart", () => {
        setTimeout(() => {
          startProgressPolling();
        }, 500); // Wait 500ms for OTA to initialize
      });

      xhr.addEventListener("load", () => {
        // Stop polling
        if (progressPolling) {
          clearInterval(progressPolling);
          setProgressPolling(null);
        }

        if (xhr.status === 200) {
          setOtaProgress(100);
          setFirmwareUploadStatus("Firmware update completed successfully!");

          // Continue polling for a bit longer to detect successful restart
          setTimeout(() => {
            if (progressPolling) {
              clearInterval(progressPolling);
              setProgressPolling(null);
            }
          }, 8000);
        } else {
          setFirmwareUploadStatus(`Update failed: ${xhr.responseText}`);
        }
      });

      xhr.addEventListener("error", () => {
        // Stop polling
        if (progressPolling) {
          clearInterval(progressPolling);
          setProgressPolling(null);
        }

        // Check if this might be a successful restart
        if (firmwareUploadProgress === 100) {
          setFirmwareUploadStatus("Upload completed! Device is restarting...");

          // Try to detect when device comes back online
          setTimeout(() => {
            checkDeviceStatus();
          }, 3000);
        } else {
          setFirmwareUploadStatus("Upload failed. Please try again.");
        }
      });

      const uploadUrl = buildApiUrl("/api/ota/upload", config?.mdns);
      xhr.open("POST", uploadUrl);
      xhr.send(formData);
    } catch (error) {
      setFirmwareUploadStatus(`Update failed: ${error.message}`);
    } finally {
      setIsUploading(false);
    }
  };

  // Upload multiple filesystem files
  const uploadFilesystemFiles = async (files) => {
    if (!files || files.length === 0) return;

    setIsUploading(true);
    setDashboardUploadProgress(0);
    setDashboardUploadStatus(`Uploading ${files.length} files...`);

    let successCount = 0;
    let failCount = 0;

    try {
      for (let i = 0; i < files.length; i++) {
        const file = files[i];
        const progress = ((i + 1) / files.length) * 100;
        setDashboardUploadProgress(progress);

        // Preserve folder structure - use webkitRelativePath if available
        const relativePath = file.webkitRelativePath || file.name;
        setDashboardUploadStatus(
          `Uploading ${relativePath} (${i + 1}/${files.length})...`
        );

        try {
          const formData = new FormData();

          // Use the original file with its relative path
          formData.append("file", file);

          // Send the relative path to preserve folder structure
          // Backend will strip the first directory level
          const uploadPath = "/" + relativePath;
          formData.append("path", uploadPath);

          const response = await fetch(
            buildApiUrl("/api/ota/filesystem", config?.mdns),
            {
              method: "POST",
              body: formData,
            }
          );

          if (response.ok) {
            const result = await response.json();
            if (result.skipped) {
              console.log(`Skipped large file: ${file.name}`);
            }
            successCount++;
          } else {
            failCount++;
            console.error(
              `Failed to upload ${file.name}:`,
              await response.text()
            );
          }
        } catch (error) {
          failCount++;
          console.error(`Error uploading ${file.name}:`, error);
        }
      }

      if (successCount === files.length) {
        setDashboardUploadStatus(
          `All ${files.length} files uploaded successfully!`
        );
        setDashboardUploadProgress(100);
      } else if (successCount > 0) {
        setDashboardUploadStatus(
          `Upload completed: ${successCount} successful, ${failCount} failed`
        );
        setDashboardUploadProgress(100);
      } else {
        setDashboardUploadStatus("All file uploads failed. Please try again.");
      }
    } catch (error) {
      setDashboardUploadStatus("Upload failed. Please try again.");
      console.error("Upload error:", error);
    } finally {
      setIsUploading(false);
    }
  };

  // Handle upload button click
  const handleUploadClick = (type) => {
    if (type === "firmware" && firmwareFile) {
      uploadFirmware(firmwareFile);
    } else if (type === "filesystem" && filesystemFiles.length > 0) {
      uploadFilesystemFiles(filesystemFiles);
    } else {
      // Open file picker
      if (type === "firmware") {
        firmwareInputRef.current?.click();
      } else {
        filesystemInputRef.current?.click();
      }
    }
  };

  // Fetch OTA status on component mount
  useEffect(() => {
    fetchOTAStatus();
  }, []);

  // Cleanup polling on unmount
  useEffect(() => {
    return () => {
      if (progressPolling) {
        clearInterval(progressPolling);
      }
    };
  }, [progressPolling]);

  if (otaLoading) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Box sx={{ textAlign: "center" }}>
          <Skeleton
            variant="circular"
            width={40}
            height={40}
            sx={{ mx: "auto", mb: 2 }}
          />
          <Skeleton variant="text" width={200} height={24} sx={{ mb: 1 }} />
          <Skeleton variant="text" width={150} height={20} />
        </Box>
      </Box>
    );
  }

  if (otaError) {
    return (
      <Alert severity="error" sx={{ marginBottom: 2 }}>
        {otaError}
      </Alert>
    );
  }

  if (!otaStatus) {
    return (
      <Box sx={{ display: "flex", justifyContent: "center", p: 3 }}>
        <Typography color="text.secondary">
          No OTA information available
        </Typography>
      </Box>
    );
  }

  return (
    <Grid container spacing={2}>
      {/* Firmware Update */}
      <Grid item xs={12}>
        <Card>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <CloudUploadIcon color="primary" />
              Firmware Update
            </Typography>
            <Typography variant="body2" color="text.secondary" sx={{ mb: 2 }}>
              Update Device Firmware (.bin)
            </Typography>

            <input
              type="file"
              ref={firmwareInputRef}
              accept=".bin"
              style={{ display: "none" }}
              onChange={(e) => handleFileSelect(e, "firmware")}
            />

            <Button
              variant="contained"
              onClick={() => handleUploadClick("firmware")}
              disabled={isUploading}
              sx={{ mb: 2 }}
            >
              Choose Firmware File
            </Button>

            {firmwareFile && (
              <Box
                sx={{
                  mb: 2,
                  p: 2,
                  bgcolor: "primary.50",
                  borderRadius: 2,
                  border: "1px solid",
                  borderColor: "primary.200",
                }}
              >
                <Typography
                  variant="body2"
                  sx={{
                    fontFamily: "monospace",
                    color: "primary.800",
                    fontWeight: 500,
                    display: "flex",
                    alignItems: "center",
                    gap: 1,
                  }}
                >
                  📁 {firmwareFile.name}
                </Typography>
                <Typography
                  variant="caption"
                  sx={{
                    color: "primary.600",
                    fontFamily: "monospace",
                    ml: 3,
                  }}
                >
                  Size: {bytesToHumanReadable(firmwareFile.size)}
                </Typography>
              </Box>
            )}

            {/* Upload Progress */}
            {firmwareUploadProgress > 0 && (
              <Box sx={{ mb: 2 }}>
                <Typography
                  variant="body2"
                  color="text.secondary"
                  sx={{ mb: 1 }}
                >
                  📤 Upload Progress:
                </Typography>
                <LinearProgress
                  variant="determinate"
                  value={firmwareUploadProgress}
                  sx={{ mb: 1 }}
                />
                <Typography variant="body2" align="center">
                  {Math.round(firmwareUploadProgress)}%
                </Typography>
              </Box>
            )}

            {/* OTA Progress */}
            {otaProgress > 0 && (
              <Box sx={{ mb: 2 }}>
                <Typography
                  variant="body2"
                  color="text.secondary"
                  sx={{ mb: 1 }}
                >
                  📦 Firmware Update Progress:
                </Typography>
                <LinearProgress
                  variant="determinate"
                  value={otaProgress}
                  sx={{ mb: 1 }}
                />
                <Typography variant="body2" align="center">
                  {Math.round(otaProgress)}%
                </Typography>
              </Box>
            )}

            {/* Status */}
            {firmwareUploadStatus && (
              <Alert
                severity={
                  firmwareUploadStatus.includes("success") ||
                  firmwareUploadStatus.includes("completed")
                    ? "success"
                    : firmwareUploadStatus.includes("error") ||
                      firmwareUploadStatus.includes("failed")
                    ? "error"
                    : "info"
                }
                sx={{ mb: 2 }}
                icon={
                  firmwareUploadStatus.includes("success") ||
                  firmwareUploadStatus.includes("completed") ? (
                    <CheckCircleIcon />
                  ) : firmwareUploadStatus.includes("error") ||
                    firmwareUploadStatus.includes("failed") ? (
                    <ErrorIcon />
                  ) : (
                    <InfoIcon />
                  )
                }
              >
                {firmwareUploadStatus}
              </Alert>
            )}
          </CardContent>
        </Card>
      </Grid>

      {/* Dashboard Update */}
      <Grid item xs={12}>
        <Card>
          <CardContent>
            <Typography
              variant="h6"
              gutterBottom
              sx={{ display: "flex", alignItems: "center", gap: 1 }}
            >
              <WebIcon color="primary" />
              Dashboard Update
            </Typography>
            <Typography variant="body2" color="text.secondary" sx={{ mb: 2 }}>
              Select multiple dashboard files or entire folders to upload to
              LittleFS. The first directory level will be automatically
              stripped.
            </Typography>

            <input
              type="file"
              ref={filesystemInputRef}
              webkitdirectory=""
              style={{ display: "none" }}
              onChange={(e) => handleFileSelect(e, "filesystem")}
            />

            <Button
              variant="contained"
              onClick={() => handleUploadClick("filesystem")}
              disabled={isUploading}
              sx={{ mb: 2 }}
            >
              Choose Files/Folders
            </Button>

            {filesystemFiles.length > 0 && (
              <Box
                sx={{
                  mb: 2,
                  p: 2,
                  bgcolor: "primary.50",
                  borderRadius: 2,
                  border: "1px solid",
                  borderColor: "primary.200",
                }}
              >
                <Typography
                  variant="body2"
                  sx={{
                    fontFamily: "monospace",
                    color: "primary.800",
                    fontWeight: 500,
                    mb: 1,
                  }}
                >
                  📁 {filesystemFiles.length} file(s) selected:
                </Typography>
                {(() => {
                  // Group files by folder
                  const folderGroups = {};
                  filesystemFiles.forEach((file) => {
                    const path = file.webkitRelativePath || file.name;
                    const folder = path.includes("/")
                      ? path.split("/")[0]
                      : "Root";
                    if (!folderGroups[folder]) {
                      folderGroups[folder] = [];
                    }
                    folderGroups[folder].push({ file, path });
                  });

                  return Object.entries(folderGroups).map(([folder, files]) => (
                    <Box key={folder} sx={{ ml: 1, mb: 1 }}>
                      <Typography
                        variant="caption"
                        sx={{
                          color: "primary.700",
                          fontFamily: "monospace",
                          fontWeight: 500,
                          display: "block",
                          mb: 0.5,
                        }}
                      >
                        📂 {folder}/
                      </Typography>
                      {files.map(({ file, path }, index) => (
                        <Box key={index} sx={{ ml: 2 }}>
                          <Typography
                            variant="caption"
                            sx={{
                              color: "primary.600",
                              fontFamily: "monospace",
                              display: "block",
                            }}
                          >
                            🌐 {path} ({bytesToHumanReadable(file.size)})
                          </Typography>
                        </Box>
                      ))}
                    </Box>
                  ));
                })()}
              </Box>
            )}

            {/* Upload Progress */}
            {dashboardUploadProgress > 0 && (
              <Box sx={{ mb: 2 }}>
                <Typography
                  variant="body2"
                  color="text.secondary"
                  sx={{ mb: 1 }}
                >
                  📤 Upload Progress:
                </Typography>
                <LinearProgress
                  variant="determinate"
                  value={dashboardUploadProgress}
                  sx={{ mb: 1 }}
                />
                <Typography variant="body2" align="center">
                  {Math.round(dashboardUploadProgress)}%
                </Typography>
              </Box>
            )}

            {/* Status */}
            {dashboardUploadStatus && (
              <Alert
                severity={
                  dashboardUploadStatus.includes("success") ||
                  dashboardUploadStatus.includes("completed")
                    ? "success"
                    : dashboardUploadStatus.includes("error") ||
                      dashboardUploadStatus.includes("failed")
                    ? "error"
                    : "info"
                }
                sx={{ mb: 2 }}
                icon={
                  dashboardUploadStatus.includes("success") ||
                  dashboardUploadStatus.includes("completed") ? (
                    <CheckCircleIcon />
                  ) : dashboardUploadStatus.includes("error") ||
                    dashboardUploadStatus.includes("failed") ? (
                    <ErrorIcon />
                  ) : (
                    <InfoIcon />
                  )
                }
              >
                {dashboardUploadStatus}
              </Alert>
            )}
          </CardContent>
        </Card>
      </Grid>
    </Grid>
  );
}
