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
  FormControl,
  InputLabel,
  Select,
  MenuItem,
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
  const [filesystemFile, setFilesystemFile] = useState(null);
  const [uploadProgress, setUploadProgress] = useState(0);
  const [otaProgress, setOtaProgress] = useState(0);
  const [uploadStatus, setUploadStatus] = useState("");
  const [uploadPath, setUploadPath] = useState("/");
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

        if (uploadProgress === 100) {
          setUploadStatus("Device is restarting... Please wait.");
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
        setUploadStatus(
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
    const file = event.target.files[0];
    if (type === "firmware") {
      setFirmwareFile(file);
      // Automatically start firmware upload when file is selected
      if (file) {
        uploadFirmware(file);
      }
    } else {
      setFilesystemFile(file);
    }
  };

  // Upload firmware
  const uploadFirmware = async (file) => {
    if (!file) return;

    setIsUploading(true);
    setUploadProgress(0);
    setOtaProgress(0);
    setUploadStatus("Initializing firmware update...");

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
      setUploadStatus("Uploading firmware...");
      const formData = new FormData();
      formData.append("file", file);

      const xhr = new XMLHttpRequest();

      // Track upload progress
      xhr.upload.addEventListener("progress", (e) => {
        if (e.lengthComputable) {
          const percentComplete = (e.loaded / e.total) * 100;
          setUploadProgress(percentComplete);
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
          setUploadStatus("Firmware update completed successfully!");

          // Continue polling for a bit longer to detect successful restart
          setTimeout(() => {
            if (progressPolling) {
              clearInterval(progressPolling);
              setProgressPolling(null);
            }
          }, 8000);
        } else {
          setUploadStatus(`Update failed: ${xhr.responseText}`);
        }
      });

      xhr.addEventListener("error", () => {
        // Stop polling
        if (progressPolling) {
          clearInterval(progressPolling);
          setProgressPolling(null);
        }

        // Check if this might be a successful restart
        if (uploadProgress === 100) {
          setUploadStatus("Upload completed! Device is restarting...");

          // Try to detect when device comes back online
          setTimeout(() => {
            checkDeviceStatus();
          }, 3000);
        } else {
          setUploadStatus("Upload failed. Please try again.");
        }
      });

      const uploadUrl = buildApiUrl("/api/ota/upload", config?.mdns);
      xhr.open("POST", uploadUrl);
      xhr.send(formData);
    } catch (error) {
      setUploadStatus(`Update failed: ${error.message}`);
    } finally {
      setIsUploading(false);
    }
  };

  // Upload filesystem file
  const uploadFilesystemFile = async (file) => {
    if (!file) return;

    setIsUploading(true);
    setUploadProgress(0);
    setUploadStatus("Uploading file...");

    try {
      const formData = new FormData();
      formData.append("file", file);
      formData.append("path", uploadPath);

      const xhr = new XMLHttpRequest();

      // Track upload progress
      xhr.upload.addEventListener("progress", (e) => {
        if (e.lengthComputable) {
          const percentComplete = (e.loaded / e.total) * 100;
          setUploadProgress(percentComplete);
        }
      });

      xhr.addEventListener("load", () => {
        if (xhr.status === 200) {
          setUploadStatus("File upload completed successfully!");
          setUploadProgress(100);
        } else {
          setUploadStatus(`Upload failed: ${xhr.responseText}`);
        }
      });

      xhr.addEventListener("error", () => {
        setUploadStatus("Upload failed. Please try again.");
      });

      const uploadUrl = buildApiUrl("/api/ota/filesystem", config?.mdns);
      xhr.open("POST", uploadUrl);
      xhr.send(formData);
    } catch (error) {
      setUploadStatus(`Upload failed: ${error.message}`);
    } finally {
      setIsUploading(false);
    }
  };

  // Handle upload button click
  const handleUploadClick = (type) => {
    if (type === "firmware" && firmwareFile) {
      uploadFirmware(firmwareFile);
    } else if (type === "filesystem" && filesystemFile) {
      uploadFilesystemFile(filesystemFile);
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
            {uploadProgress > 0 && (
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
                  value={uploadProgress}
                  sx={{ mb: 1 }}
                />
                <Typography variant="body2" align="center">
                  {Math.round(uploadProgress)}%
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
            {uploadStatus && (
              <Alert
                severity={
                  uploadStatus.includes("success") ||
                  uploadStatus.includes("completed")
                    ? "success"
                    : uploadStatus.includes("error") ||
                      uploadStatus.includes("failed")
                    ? "error"
                    : "info"
                }
                sx={{ mb: 2 }}
                icon={
                  uploadStatus.includes("success") ||
                  uploadStatus.includes("completed") ? (
                    <CheckCircleIcon />
                  ) : uploadStatus.includes("error") ||
                    uploadStatus.includes("failed") ? (
                    <ErrorIcon />
                  ) : (
                    <InfoIcon />
                  )
                }
              >
                {uploadStatus}
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
              Upload dashboard files to update the UI stored on LittleFS.
            </Typography>

            <input
              type="file"
              ref={filesystemInputRef}
              style={{ display: "none" }}
              onChange={(e) => handleFileSelect(e, "filesystem")}
            />

            <FormControl fullWidth sx={{ mb: 2 }}>
              <InputLabel>Upload Path</InputLabel>
              <Select
                value={uploadPath}
                label="Upload Path"
                onChange={(e) => setUploadPath(e.target.value)}
              >
                <MenuItem value="/">Root (/)</MenuItem>
                <MenuItem value="/static">Static Files (/static)</MenuItem>
                <MenuItem value="/data">Data (/data)</MenuItem>
                <MenuItem value="/config">Config (/config)</MenuItem>
              </Select>
            </FormControl>

            <Button
              variant="contained"
              onClick={() => handleUploadClick("filesystem")}
              disabled={isUploading}
              sx={{ mb: 2 }}
            >
              {filesystemFile ? "Upload File" : "Choose File"}
            </Button>

            {filesystemFile && (
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
                  🌐 {filesystemFile.name}
                </Typography>
                <Typography
                  variant="caption"
                  sx={{
                    color: "primary.600",
                    fontFamily: "monospace",
                    ml: 3,
                  }}
                >
                  Size: {bytesToHumanReadable(filesystemFile.size)}
                </Typography>
              </Box>
            )}

            {/* Upload Progress */}
            {uploadProgress > 0 && (
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
                  value={uploadProgress}
                  sx={{ mb: 1 }}
                />
                <Typography variant="body2" align="center">
                  {Math.round(uploadProgress)}%
                </Typography>
              </Box>
            )}

            {/* Status */}
            {uploadStatus && (
              <Alert
                severity={
                  uploadStatus.includes("success") ||
                  uploadStatus.includes("completed")
                    ? "success"
                    : uploadStatus.includes("error") ||
                      uploadStatus.includes("failed")
                    ? "error"
                    : "info"
                }
                sx={{ mb: 2 }}
                icon={
                  uploadStatus.includes("success") ||
                  uploadStatus.includes("completed") ? (
                    <CheckCircleIcon />
                  ) : uploadStatus.includes("error") ||
                    uploadStatus.includes("failed") ? (
                    <ErrorIcon />
                  ) : (
                    <InfoIcon />
                  )
                }
              >
                {uploadStatus}
              </Alert>
            )}
          </CardContent>
        </Card>
      </Grid>
    </Grid>
  );
}
