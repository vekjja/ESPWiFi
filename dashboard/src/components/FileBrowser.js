import React, { useState, useEffect, useCallback, useRef } from "react";
import {
  Box,
  Paper,
  Typography,
  Button,
  Dialog,
  DialogTitle,
  DialogContent,
  DialogActions,
  TextField,
  Alert,
  CircularProgress,
  LinearProgress,
  ToggleButton,
  ToggleButtonGroup,
  Chip,
  List,
  ListItem,
  ListItemIcon,
  ListItemText,
  ListItemSecondaryAction,
  IconButton,
  Menu,
  MenuItem,
  Breadcrumbs,
  Link,
} from "@mui/material";
import { getFetchOptions } from "../utils/apiUtils";
import { getAuthHeader } from "../utils/authUtils";
import {
  Storage,
  Upload,
  Download,
  Folder,
  InsertDriveFile,
  MoreVert,
  Home,
  Refresh,
} from "@mui/icons-material";
import { useTheme } from "@mui/material";
import { getDeleteIcon, getEditIcon } from "../utils/themeUtils";

// Helper function to format bytes
const formatBytes = (bytes) => {
  if (bytes === 0) return "0 B";
  const k = 1024;
  const sizes = ["B", "KB", "MB", "GB"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i];
};

// Helper function to infer content type from file extension
const getContentTypeFromExtension = (fileName) => {
  const ext = fileName.split(".").pop()?.toLowerCase() || "";
  const contentTypes = {
    // Images
    jpg: "image/jpeg",
    jpeg: "image/jpeg",
    png: "image/png",
    gif: "image/gif",
    webp: "image/webp",
    svg: "image/svg+xml",
    bmp: "image/bmp",
    ico: "image/x-icon",
    // Videos
    mp4: "video/mp4",
    webm: "video/webm",
    ogg: "video/ogg",
    avi: "video/x-msvideo",
    mov: "video/quicktime",
    mkv: "video/x-matroska",
    // Audio
    mp3: "audio/mpeg",
    wav: "audio/wav",
    oga: "audio/ogg",
    flac: "audio/flac",
    // Documents
    pdf: "application/pdf",
    // Text files
    txt: "text/plain",
    log: "text/plain",
    json: "application/json",
    xml: "application/xml",
    html: "text/html",
    htm: "text/html",
    css: "text/css",
    js: "text/javascript",
    md: "text/markdown",
    ini: "text/plain",
    conf: "text/plain",
    cfg: "text/plain",
    yml: "text/yaml",
    yaml: "text/yaml",
  };
  return contentTypes[ext] || null;
};

const FileBrowserComponent = ({ config, deviceOnline }) => {
  const theme = useTheme();
  const DeleteIcon = getDeleteIcon(theme);
  const EditIcon = getEditIcon(theme);

  // State management
  const [files, setFiles] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [info, setInfo] = useState(null);
  const [newFolderDialog, setNewFolderDialog] = useState({
    open: false,
    folderName: "",
  });
  const [currentPath, setCurrentPath] = useState("/");
  const [fileSystem, setFileSystem] = useState("lfs");
  const [uploadProgress, setUploadProgress] = useState(0);
  const [isUploading, setIsUploading] = useState(false);
  const [downloadProgress, setDownloadProgress] = useState(0);
  const [isDownloading, setIsDownloading] = useState(false);
  const [downloadingFileName, setDownloadingFileName] = useState("");
  const [downloadIndeterminate, setDownloadIndeterminate] = useState(false);
  const [storageInfo, setStorageInfo] = useState({
    total: 0,
    used: 0,
    free: 0,
    loading: false,
  });
  const [renameDialog, setRenameDialog] = useState({
    open: false,
    file: null,
    newName: "",
  });
  const [deleteDialog, setDeleteDialog] = useState({ open: false, files: [] });
  const [contextMenu, setContextMenu] = useState({
    mouseX: null,
    mouseY: null,
    file: null,
  });

  // Refs for cleanup and request management
  const abortControllerRef = useRef(null);
  const uploadXhrRef = useRef(null);
  const downloadXhrRef = useRef(null);

  const apiURL = config?.apiURL || "";

  // Helper function to reset download state
  const resetDownloadState = useCallback(() => {
    setIsDownloading(false);
    setDownloadProgress(0);
    setDownloadingFileName("");
    setDownloadIndeterminate(false);
    downloadXhrRef.current = null;
  }, []);

  // Cleanup function for aborting requests
  useEffect(() => {
    return () => {
      if (abortControllerRef.current) {
        abortControllerRef.current.abort();
      }
      if (uploadXhrRef.current) {
        uploadXhrRef.current.abort();
      }
      if (downloadXhrRef.current) {
        downloadXhrRef.current.abort();
      }
    };
  }, []);

  // Generic fetch with retry logic
  const fetchWithRetry = useCallback(
    async (url, options = {}, retries = 2, timeout = 10000) => {
      for (let i = 0; i <= retries; i++) {
        try {
          const controller = new AbortController();
          const timeoutId = setTimeout(() => controller.abort(), timeout);

          const response = await fetch(
            url,
            getFetchOptions({ ...options, signal: controller.signal })
          );

          clearTimeout(timeoutId);

          if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
          }

          return response;
        } catch (err) {
          if (i === retries) throw err;
          if (err.name === "AbortError") {
            throw new Error("Request timed out");
          }
          // Wait before retry (exponential backoff)
          await new Promise((resolve) => setTimeout(resolve, 500 * (i + 1)));
        }
      }
    },
    []
  );

  // Open file with authentication and progress tracking
  const openFileWithAuth = useCallback(
    async (file) => {
      const fileUrl = `${apiURL}/${fileSystem}${file.path}`;

      setIsDownloading(true);
      setDownloadProgress(0);
      setDownloadingFileName(file.name);
      setDownloadIndeterminate(false);
      setError(null);

      // Open a blank window immediately (within user action context to avoid popup blocker)
      let newWindow = null;
      try {
        newWindow = window.open("", "_blank");
        if (newWindow) {
          newWindow.document.write(`
            <html>
              <head>
                <title>Loading ${file.name}...</title>
                <style>
                  body {
                    margin: 0;
                    display: flex;
                    align-items: center;
                    justify-content: center;
                    height: 100vh;
                    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
                    background: #1a1a1a;
                    color: #fff;
                  }
                  .container {
                    text-align: center;
                    max-width: 400px;
                    padding: 20px;
                  }
                  .filename {
                    font-size: 18px;
                    margin-bottom: 24px;
                    word-break: break-word;
                  }
                  .progress-bar {
                    width: 100%;
                    height: 6px;
                    background: rgba(71, 255, 240, 0.1);
                    border-radius: 3px;
                    overflow: hidden;
                    margin-bottom: 12px;
                  }
                  .progress-fill {
                    height: 100%;
                    background: #47FFF0;
                    width: 0%;
                    transition: width 0.3s ease;
                  }
                  .progress-text {
                    font-size: 14px;
                    color: #888;
                  }
                  .spinner {
                    border: 3px solid rgba(71, 255, 240, 0.1);
                    border-top: 3px solid #47FFF0;
                    border-radius: 50%;
                    width: 40px;
                    height: 40px;
                    animation: spin 1s linear infinite;
                    margin: 20px auto;
                  }
                  @keyframes spin {
                    0% { transform: rotate(0deg); }
                    100% { transform: rotate(360deg); }
                  }
                </style>
              </head>
              <body>
                <div class="container">
                  <div class="spinner"></div>
                  <div class="filename" id="filename">${file.name}</div>
                  <div class="progress-bar">
                    <div class="progress-fill" id="progress"></div>
                  </div>
                  <div class="progress-text" id="status">Starting download...</div>
                </div>
              </body>
            </html>
          `);
          newWindow.document.close();
        }
      } catch (err) {
        console.warn("Could not open new window (popup blocked?):", err);
      }

      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        downloadXhrRef.current = xhr;

        xhr.open("GET", fileUrl, true);
        xhr.responseType = "blob";

        // Track download progress and update the new window
        xhr.addEventListener("progress", (event) => {
          if (event.lengthComputable) {
            const percentComplete = Math.round(
              (event.loaded / event.total) * 100
            );
            setDownloadProgress(percentComplete);
            setDownloadIndeterminate(false);

            // Update progress in the new window
            if (newWindow && !newWindow.closed && newWindow.document) {
              try {
                const progressEl =
                  newWindow.document.getElementById("progress");
                const statusEl = newWindow.document.getElementById("status");
                if (progressEl) progressEl.style.width = percentComplete + "%";
                if (statusEl)
                  statusEl.textContent = `Loading... ${percentComplete}%`;
              } catch (e) {
                // Ignore cross-origin or access errors
              }
            }
          } else {
            // No Content-Length header, show indeterminate progress
            setDownloadIndeterminate(true);
            if (newWindow && !newWindow.closed && newWindow.document) {
              try {
                const statusEl = newWindow.document.getElementById("status");
                if (statusEl) statusEl.textContent = "Loading...";
              } catch (e) {
                // Ignore cross-origin or access errors
              }
            }
          }
        });

        xhr.addEventListener("load", () => {
          if (xhr.status >= 200 && xhr.status < 300) {
            try {
              // Get content type from response or infer from extension
              let contentType =
                xhr.getResponseHeader("content-type") ||
                getContentTypeFromExtension(file.name) ||
                "application/octet-stream";

              const blob = xhr.response;
              const blobUrl = URL.createObjectURL(
                new Blob([blob], { type: contentType })
              );

              // If we opened a window earlier, update it with the blob
              if (newWindow && !newWindow.closed) {
                newWindow.location.replace(blobUrl); // Use replace instead of href
              } else {
                // Try to open a new window (may be blocked)
                const fallbackWindow = window.open(blobUrl, "_blank");
                if (!fallbackWindow) {
                  // Popup was blocked, create a download link instead
                  const a = document.createElement("a");
                  a.href = blobUrl;
                  a.target = "_blank";
                  a.rel = "noopener noreferrer";
                  document.body.appendChild(a);
                  a.click();
                  document.body.removeChild(a);
                  setInfo(
                    `Opened ${file.name} (if popup was blocked, check downloads)`
                  );
                } else {
                  setInfo(`Opened ${file.name}`);
                }
              }

              setTimeout(() => URL.revokeObjectURL(blobUrl), 60000);
              resolve();
            } catch (err) {
              if (newWindow && !newWindow.closed) newWindow.close();
              setError(`Failed to open file: ${err.message}`);
              reject(err);
            } finally {
              resetDownloadState();
            }
          } else {
            if (newWindow && !newWindow.closed) newWindow.close();
            const errorMsg =
              xhr.status === 408
                ? "Request timed out - device may be busy"
                : `Failed to open file: HTTP ${xhr.status}`;
            setError(errorMsg);
            resetDownloadState();
            reject(new Error(errorMsg));
          }
        });

        xhr.addEventListener("error", () => {
          if (newWindow && !newWindow.closed) newWindow.close();
          setError("Failed to open file: Network error");
          resetDownloadState();
          reject(new Error("Network error"));
        });

        xhr.addEventListener("timeout", () => {
          if (newWindow && !newWindow.closed) newWindow.close();
          setError("Request timed out - device may be busy");
          resetDownloadState();
          reject(new Error("Timeout"));
        });

        xhr.timeout = 60000; // 60 second timeout for large files
        xhr.send();
      });
    },
    [apiURL, fileSystem, resetDownloadState]
  );

  // Download file with authentication and progress tracking
  const downloadFileWithAuth = useCallback(
    async (file) => {
      const fileUrl = `${apiURL}/${fileSystem}${file.path}`;

      setIsDownloading(true);
      setDownloadProgress(0);
      setDownloadingFileName(file.name);
      setDownloadIndeterminate(false);
      setError(null);

      return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        downloadXhrRef.current = xhr;

        xhr.open("GET", fileUrl, true);
        xhr.responseType = "blob";

        // Track download progress
        xhr.addEventListener("progress", (event) => {
          if (event.lengthComputable) {
            const percentComplete = Math.round(
              (event.loaded / event.total) * 100
            );
            setDownloadProgress(percentComplete);
            setDownloadIndeterminate(false);
          } else {
            // No Content-Length header, show indeterminate progress
            setDownloadIndeterminate(true);
          }
        });

        xhr.addEventListener("load", () => {
          if (xhr.status >= 200 && xhr.status < 300) {
            try {
              const contentType =
                xhr.getResponseHeader("content-type") ||
                "application/octet-stream";
              const blob = xhr.response;
              const blobUrl = URL.createObjectURL(
                new Blob([blob], { type: contentType })
              );

              const a = document.createElement("a");
              a.href = blobUrl;
              a.download = file?.name || "download";
              document.body.appendChild(a);
              a.click();
              a.remove();

              setTimeout(() => URL.revokeObjectURL(blobUrl), 60000);

              setInfo(`Downloaded ${file.name}`);
              resolve();
            } catch (err) {
              setError(`Failed to download file: ${err.message}`);
              reject(err);
            } finally {
              resetDownloadState();
            }
          } else {
            const errorMsg =
              xhr.status === 408
                ? "Request timed out - device may be busy"
                : `Failed to download file: HTTP ${xhr.status}`;
            setError(errorMsg);
            resetDownloadState();
            reject(new Error(errorMsg));
          }
        });

        xhr.addEventListener("error", () => {
          setError("Failed to download file: Network error");
          resetDownloadState();
          reject(new Error("Network error"));
        });

        xhr.addEventListener("timeout", () => {
          setError("Request timed out - device may be busy");
          resetDownloadState();
          reject(new Error("Timeout"));
        });

        xhr.timeout = 60000; // 60 second timeout for large files
        xhr.send();
      });
    },
    [apiURL, fileSystem, resetDownloadState]
  );

  // Fetch storage information
  const fetchStorageInfo = useCallback(
    async (fs = fileSystem) => {
      setStorageInfo((prev) => ({ ...prev, loading: true }));

      try {
        const response = await fetchWithRetry(
          `${apiURL}/api/storage?fs=${encodeURIComponent(fs)}`
        );
        const data = await response.json();
        setStorageInfo({
          total: data.total || 0,
          used: data.used || 0,
          free: data.free || 0,
          loading: false,
        });
      } catch (err) {
        console.error(`Failed to fetch storage info: ${err.message}`);
        setStorageInfo((prev) => ({ ...prev, loading: false }));
      }
    },
    [fileSystem, apiURL, fetchWithRetry]
  );

  // Fetch files with retry and better error handling
  const fetchFiles = useCallback(
    async (path = currentPath, fs = fileSystem) => {
      setLoading(true);
      setError(null);

      try {
        const response = await fetchWithRetry(
          `${apiURL}/api/files?fs=${fs}&path=${encodeURIComponent(path)}`
        );

        const data = await response.json();
        setFiles(data.files || []);
        setCurrentPath(path);
        setFileSystem(fs);
        fetchStorageInfo(fs);
      } catch (err) {
        let errorMsg = "Failed to load files";
        if (err.message === "Request timed out") {
          errorMsg = "Device is busy. Please wait a moment and try again.";
        } else if (
          err.message.includes("NetworkError") ||
          err.message.includes("fetch")
        ) {
          errorMsg = "Cannot connect to device. Check your connection.";
        } else {
          errorMsg = `Failed to load files: ${err.message}`;
        }
        setError(errorMsg);
        console.error("Error fetching files:", err);
      } finally {
        setLoading(false);
      }
    },
    [apiURL, fetchStorageInfo, fetchWithRetry]
  );

  // Handle file system change
  const handleFileSystemChange = (event, newFileSystem) => {
    if (newFileSystem !== null) {
      setFileSystem(newFileSystem);
      setCurrentPath("/");
      fetchFiles("/", newFileSystem);
    }
  };

  // Handle file click
  const handleFileClick = (file) => {
    if (file.isDirectory) {
      const newPath = currentPath.endsWith("/")
        ? currentPath + file.name
        : currentPath + "/" + file.name;
      fetchFiles(newPath, fileSystem);
    } else {
      openFileWithAuth(file);
    }
  };

  // Handle new folder creation
  const handleCreateFolder = async () => {
    if (!newFolderDialog.folderName.trim()) {
      setError("Please enter a folder name");
      return;
    }

    setLoading(true);
    setError(null);

    try {
      await fetchWithRetry(`${apiURL}/api/files/mkdir`, {
        method: "POST",
        body: JSON.stringify({
          fs: fileSystem,
          path: currentPath,
          name: newFolderDialog.folderName.trim(),
        }),
      });

      setNewFolderDialog({ open: false, folderName: "" });
      fetchFiles(currentPath, fileSystem);
      setInfo(`Folder "${newFolderDialog.folderName}" created successfully`);
    } catch (err) {
      setError(`Failed to create folder: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  // Context menu handlers
  const handleContextMenu = (event, file) => {
    event.preventDefault();
    setContextMenu({
      mouseX: event.clientX - 2,
      mouseY: event.clientY - 4,
      file: file,
    });
  };

  const handleContextMenuClose = () => {
    setContextMenu({
      mouseX: null,
      mouseY: null,
      file: null,
    });
  };

  const handleRename = () => {
    if (contextMenu.file) {
      setRenameDialog({
        open: true,
        file: contextMenu.file,
        newName: contextMenu.file.name,
      });
    }
    handleContextMenuClose();
  };

  const handleDelete = () => {
    if (contextMenu.file) {
      setDeleteDialog({ open: true, files: [contextMenu.file] });
    }
    handleContextMenuClose();
  };

  // Rename file
  const handleRenameSubmit = async () => {
    if (!renameDialog.file || !renameDialog.newName.trim()) return;

    try {
      await fetchWithRetry(
        `${apiURL}/api/files/rename?fs=${encodeURIComponent(
          fileSystem
        )}&oldPath=${encodeURIComponent(
          renameDialog.file.path
        )}&newName=${encodeURIComponent(renameDialog.newName.trim())}`,
        { method: "POST" }
      );

      setRenameDialog({ open: false, file: null, newName: "" });
      fetchFiles(currentPath, fileSystem);
      setInfo("File renamed successfully");
    } catch (err) {
      setError(`Failed to rename file: ${err.message}`);
    }
  };

  // Delete files
  const handleDeleteSubmit = async () => {
    try {
      for (const file of deleteDialog.files) {
        await fetchWithRetry(
          `${apiURL}/api/files/delete?fs=${encodeURIComponent(
            fileSystem
          )}&path=${encodeURIComponent(file.path)}`,
          { method: "POST" }
        );
      }

      setDeleteDialog({ open: false, files: [] });
      fetchFiles(currentPath, fileSystem);
      setInfo(
        `${deleteDialog.files.length} ${
          deleteDialog.files.length === 1 ? "file" : "files"
        } deleted successfully`
      );
    } catch (err) {
      setDeleteDialog({ open: false, files: [] });
      setError(`Failed to delete files: ${err.message}`);
    }
  };

  // Upload file with progress tracking
  const handleUpload = (event) => {
    const file = event.target.files[0];
    if (!file) return;

    // Check if file is too large for available storage
    if (storageInfo.total > 0 && file.size > storageInfo.free) {
      setError(
        `File too large: ${formatBytes(file.size)}. Only ${formatBytes(
          storageInfo.free
        )} available.`
      );
      event.target.value = "";
      return;
    }

    // Reset progress and start uploading
    setUploadProgress(0);
    setIsUploading(true);
    setError(null);

    // Create FormData with file
    const formData = new FormData();
    formData.append("file", file);

    // Add fs and path as URL parameters
    const url = `${apiURL}/api/files/upload?fs=${encodeURIComponent(
      fileSystem
    )}&path=${encodeURIComponent(currentPath)}`;

    // Use XMLHttpRequest for progress tracking
    const xhr = new XMLHttpRequest();
    uploadXhrRef.current = xhr;

    // Track upload progress
    xhr.upload.addEventListener("progress", (event) => {
      if (event.lengthComputable) {
        const percentComplete = Math.round((event.loaded / event.total) * 100);
        setUploadProgress(percentComplete);
      }
    });

    // Handle successful upload
    xhr.addEventListener("load", () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        setUploadProgress(100);
        setInfo("File uploaded successfully");

        // Small delay before refreshing to let device finish processing
        setTimeout(() => {
          setIsUploading(false);
          fetchFiles(currentPath, fileSystem);
        }, 500);
      } else {
        // Parse error message
        let errorMsg = `Upload failed: HTTP ${xhr.status}`;
        try {
          const responseText = xhr.responseText;
          if (responseText) {
            const errorJson = JSON.parse(responseText);
            if (errorJson.error) {
              errorMsg = `Upload failed: ${errorJson.error}`;
            }
          }
        } catch (e) {
          // Use default error
        }
        setError(errorMsg);
        setIsUploading(false);
        setUploadProgress(0);
      }
      event.target.value = "";
      uploadXhrRef.current = null;
    });

    // Handle upload errors
    xhr.addEventListener("error", () => {
      setError("Upload failed: Network error");
      setIsUploading(false);
      setUploadProgress(0);
      event.target.value = "";
      uploadXhrRef.current = null;
    });

    // Handle upload abort
    xhr.addEventListener("abort", () => {
      setIsUploading(false);
      setUploadProgress(0);
      event.target.value = "";
      uploadXhrRef.current = null;
    });

    // Start the upload
    xhr.open("POST", url);

    // Add authentication header
    const authHeader = getAuthHeader();
    if (authHeader) {
      xhr.setRequestHeader("Authorization", authHeader);
    }

    xhr.send(formData);
  };

  // Navigate to parent directory
  const handleParentDirectory = () => {
    if (currentPath !== "/") {
      const parentPath =
        currentPath.substring(0, currentPath.lastIndexOf("/")) || "/";
      fetchFiles(parentPath, fileSystem);
    }
  };

  // Generate breadcrumbs
  const generateBreadcrumbs = () => {
    const pathParts = currentPath.split("/").filter((part) => part !== "");
    const breadcrumbs = [
      <Link
        key="root"
        component="button"
        onClick={() => fetchFiles("/", fileSystem)}
        sx={{ display: "flex", alignItems: "center", gap: 0.5 }}
        underline="none"
      >
        <Home fontSize="small" />
        Root
      </Link>,
    ];

    let currentBreadcrumbPath = "";
    pathParts.forEach((part, index) => {
      currentBreadcrumbPath += "/" + part;
      const pathToNavigate = currentBreadcrumbPath;
      breadcrumbs.push(
        <Link
          key={index}
          component="button"
          onClick={() => fetchFiles(pathToNavigate, fileSystem)}
          underline="none"
        >
          {part}
        </Link>
      );
    });

    return breadcrumbs;
  };

  // Initialize - only load on mount if device is online
  useEffect(() => {
    if (config && deviceOnline) {
      const initialFs = config?.sd?.initialized === true ? "sd" : "lfs";
      fetchFiles("/", initialFs);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [config, deviceOnline]);

  // Show offline message but don't block the component
  const showOfflineWarning = !deviceOnline && files.length === 0;

  return (
    <Box
      sx={{
        height: "100%",
        flex: 1,
        display: "flex",
        flexDirection: "column",
        width: "100%",
        maxWidth: "100%",
        minWidth: 0,
        mx: 0,
        minHeight: 0,
        "& .MuiPaper-root": {
          p: { xs: 1, sm: 1.5 },
        },
      }}
    >
      {/* Header - Fixed */}
      <Paper sx={{ flexShrink: 0, mb: 1, p: { xs: 1.5, sm: 2 } }}>
        <Box
          sx={{
            mb: 1,
            display: { xs: "grid", sm: "flex" },
            gridTemplateColumns: { xs: "1fr 1fr", sm: "none" },
            alignItems: { xs: "stretch", sm: "center" },
            justifyContent: { xs: "stretch", sm: "center" },
            gap: { xs: 1, sm: 2 },
            flexWrap: { sm: "wrap" },
          }}
        >
          <ToggleButtonGroup
            value={fileSystem}
            exclusive
            onChange={handleFileSystemChange}
            size="small"
            disabled={loading || isUploading || isDownloading}
            sx={{
              width: { xs: "100%", sm: "auto" },
              "& .MuiToggleButton-root": {
                minWidth: { xs: 0, sm: "100px" },
                height: "32px",
                flex: 1,
              },
            }}
          >
            {config?.sd?.initialized === true && (
              <ToggleButton value="sd">
                <Storage sx={{ mr: 0.5 }} />
                <Box sx={{ display: { xs: "none", sm: "inline" } }}>
                  SD Card
                </Box>
                <Box sx={{ display: { xs: "inline", sm: "none" } }}>SD</Box>
              </ToggleButton>
            )}
            <ToggleButton value="lfs">
              <Storage sx={{ mr: 0.5 }} />
              <Box sx={{ display: { xs: "none", sm: "inline" } }}>Device</Box>
              <Box sx={{ display: { xs: "inline", sm: "none" } }}>Int</Box>
            </ToggleButton>
          </ToggleButtonGroup>

          <Button
            variant="contained"
            component="label"
            startIcon={<Upload />}
            disabled={loading || isUploading || isDownloading}
            size="small"
            sx={{
              width: { xs: "100%", sm: "auto" },
              minWidth: { xs: 0, sm: "80px" },
              px: 1,
              height: "32px",
            }}
          >
            <Box sx={{ display: { xs: "none", sm: "inline" } }}>
              {isUploading ? `Uploading... ${uploadProgress}%` : "Upload"}
            </Box>
            <Box sx={{ display: { xs: "inline", sm: "none" } }}>
              {isUploading ? `${uploadProgress}%` : "Upload"}
            </Box>
            <input type="file" hidden onChange={handleUpload} />
          </Button>

          <Button
            variant="outlined"
            startIcon={<Folder />}
            onClick={() => setNewFolderDialog({ open: true, folderName: "" })}
            disabled={loading || isUploading || isDownloading}
            size="small"
            sx={{
              width: { xs: "100%", sm: "auto" },
              minWidth: { xs: 0, sm: "100px" },
              px: 1,
              height: "32px",
            }}
          >
            <Box sx={{ display: { xs: "none", sm: "inline" } }}>New Folder</Box>
            <Box sx={{ display: { xs: "inline", sm: "none" } }}>Folder</Box>
          </Button>

          <IconButton
            onClick={() => fetchFiles(currentPath, fileSystem)}
            disabled={loading || isUploading || isDownloading}
            size="small"
            sx={{ height: "32px", width: "32px" }}
          >
            <Refresh />
          </IconButton>
        </Box>

        {/* Upload Progress Bar */}
        {isUploading && (
          <Box sx={{ width: "100%", mt: 1 }}>
            <LinearProgress
              variant="determinate"
              value={uploadProgress}
              sx={{
                height: 6,
                borderRadius: 3,
                backgroundColor: "rgba(0, 0, 0, 0.1)",
                "& .MuiLinearProgress-bar": {
                  borderRadius: 3,
                },
              }}
            />
            <Typography
              variant="caption"
              sx={{
                display: "block",
                textAlign: "center",
                mt: 0.5,
                color: "text.secondary",
              }}
            >
              Uploading... {uploadProgress}%
            </Typography>
          </Box>
        )}

        {/* Download Progress */}
        {isDownloading && (
          <Box sx={{ width: "100%", mt: 1 }}>
            <LinearProgress
              variant={downloadIndeterminate ? "indeterminate" : "determinate"}
              value={downloadIndeterminate ? undefined : downloadProgress}
              sx={{
                height: 6,
                borderRadius: 3,
                backgroundColor: "rgba(0, 0, 0, 0.1)",
                "& .MuiLinearProgress-bar": {
                  borderRadius: 3,
                  backgroundColor: theme.palette.primary.main,
                },
              }}
            />
            <Typography
              variant="caption"
              sx={{
                display: "block",
                textAlign: "center",
                mt: 0.5,
                color: "text.secondary",
              }}
            >
              {downloadIndeterminate
                ? `Loading ${downloadingFileName}...`
                : downloadProgress < 100
                ? `Loading ${downloadingFileName}... ${downloadProgress}%`
                : `Opening ${downloadingFileName}...`}
            </Typography>
          </Box>
        )}

        {/* Storage Information */}
        {storageInfo.total > 0 && (
          <Box
            sx={{
              mb: 2,
              p: 1.5,
              backgroundColor: "rgba(0, 0, 0, 0.02)",
              borderRadius: 1,
              border: "1px solid",
              borderColor: "divider",
            }}
          >
            <Box
              sx={{
                display: "flex",
                alignItems: "center",
                justifyContent: "space-between",
                mb: 1,
              }}
            >
              <Typography
                variant="caption"
                sx={{
                  fontWeight: 500,
                  color: "text.primary",
                  textTransform: "uppercase",
                  letterSpacing: 0.5,
                }}
              >
                {fileSystem === "sd" ? "SD Card" : "Device"} Storage
              </Typography>
              <Typography
                variant="caption"
                sx={{
                  color: "text.secondary",
                  fontFamily: "monospace",
                }}
              >
                {formatBytes(storageInfo.used)} /{" "}
                {formatBytes(storageInfo.total)}
              </Typography>
            </Box>
            <Box sx={{ display: "flex", alignItems: "center", gap: 1 }}>
              <LinearProgress
                variant="determinate"
                value={(storageInfo.used / storageInfo.total) * 100}
                sx={{
                  flex: 1,
                  height: 4,
                  borderRadius: 2,
                  backgroundColor: "rgba(0, 0, 0, 0.08)",
                  "& .MuiLinearProgress-bar": {
                    borderRadius: 2,
                    backgroundColor: theme.palette.primary.main,
                  },
                }}
              />
              <Typography
                variant="caption"
                sx={{
                  color: "text.secondary",
                  fontFamily: "monospace",
                  minWidth: "60px",
                  textAlign: "right",
                }}
              >
                {formatBytes(storageInfo.free)}
              </Typography>
            </Box>
          </Box>
        )}

        {/* Breadcrumbs */}
        <Breadcrumbs
          sx={{
            mb: 1,
            overflow: "hidden",
            justifyContent: "center",
            "& .MuiBreadcrumbs-ol": {
              flexWrap: "nowrap",
              justifyContent: "center",
            },
            "& .MuiBreadcrumbs-li": {
              overflow: "hidden",
              textOverflow: "ellipsis",
              whiteSpace: "nowrap",
              maxWidth: { xs: "100px", sm: "150px" },
            },
            "& .MuiLink-root, & .MuiTypography-root": {
              fontSize: { xs: "0.75rem", sm: "0.8125rem" },
              lineHeight: 1.2,
            },
          }}
        >
          {generateBreadcrumbs()}
        </Breadcrumbs>

        {/* Offline Warning */}
        {showOfflineWarning && (
          <Alert severity="warning" sx={{ mb: 2 }}>
            Device appears offline. Some features may not work.
          </Alert>
        )}

        {/* Info Display */}
        {info && (
          <Alert
            severity="info"
            sx={{
              mb: 2,
              backgroundColor: theme.palette.primary.main + "15",
              color: theme.palette.primary.main,
              border: `1px solid ${theme.palette.primary.main}30`,
              "& .MuiAlert-icon": {
                color: theme.palette.primary.main,
              },
            }}
            onClose={() => setInfo(null)}
          >
            {info}
          </Alert>
        )}

        {/* Error Display */}
        {error && (
          <Alert severity="error" sx={{ mb: 2 }} onClose={() => setError(null)}>
            {error}
          </Alert>
        )}
      </Paper>

      {/* File List - Scrollable */}
      <Paper
        sx={{
          flex: 1,
          overflow: "hidden",
          display: "flex",
          flexDirection: "column",
          minHeight: 0,
        }}
      >
        {loading ? (
          <Box
            sx={{
              display: "flex",
              justifyContent: "center",
              alignItems: "center",
              height: "100%",
            }}
          >
            <CircularProgress />
          </Box>
        ) : (
          <List sx={{ flex: 1, overflow: "auto", p: 0 }}>
            {/* Parent directory link */}
            {currentPath !== "/" && (
              <ListItem
                onClick={handleParentDirectory}
                sx={{
                  backgroundColor: "action.hover",
                  cursor: "pointer",
                  "&:hover": { backgroundColor: "action.selected" },
                }}
              >
                <ListItemIcon>
                  <Folder color="primary" />
                </ListItemIcon>
                <ListItemText primary=".." />
              </ListItem>
            )}

            {/* Files and directories */}
            {files.map((file, index) => (
              <ListItem
                key={index}
                onClick={() => handleFileClick(file)}
                onContextMenu={(e) => handleContextMenu(e, file)}
                sx={{
                  cursor: "pointer",
                  "&:hover": { backgroundColor: "action.hover" },
                }}
              >
                <ListItemIcon>
                  {file.isDirectory ? (
                    <Folder color="primary" />
                  ) : (
                    <InsertDriveFile color="action" />
                  )}
                </ListItemIcon>
                <ListItemText
                  primary={file.name}
                  secondary={
                    file.isDirectory ? "Folder" : formatBytes(file.size)
                  }
                  sx={{
                    "& .MuiListItemText-primary": {
                      overflow: "hidden",
                      textOverflow: "ellipsis",
                      whiteSpace: "nowrap",
                      maxWidth: { xs: "200px", sm: "400px" },
                    },
                    "& .MuiListItemText-secondary": {
                      overflow: "hidden",
                      textOverflow: "ellipsis",
                      whiteSpace: "nowrap",
                    },
                  }}
                />
                <ListItemSecondaryAction>
                  <IconButton
                    edge="end"
                    onClick={(e) => {
                      e.stopPropagation();
                      handleContextMenu(e, file);
                    }}
                  >
                    <MoreVert />
                  </IconButton>
                </ListItemSecondaryAction>
              </ListItem>
            ))}

            {files.length === 0 && !loading && (
              <ListItem>
                <ListItemText
                  primary="No files found"
                  sx={{ textAlign: "center", color: "text.secondary" }}
                />
              </ListItem>
            )}
          </List>
        )}
      </Paper>

      {/* Context Menu */}
      <Menu
        open={contextMenu.mouseY !== null}
        onClose={handleContextMenuClose}
        anchorReference="anchorPosition"
        anchorPosition={
          contextMenu.mouseY !== null && contextMenu.mouseX !== null
            ? { top: contextMenu.mouseY, left: contextMenu.mouseX }
            : undefined
        }
      >
        <MenuItem onClick={handleRename}>
          <EditIcon sx={{ mr: 1 }} />
          Rename
        </MenuItem>
        <MenuItem onClick={handleDelete} sx={{ color: "error.main" }}>
          <DeleteIcon sx={{ mr: 1 }} />
          Delete
        </MenuItem>
        {contextMenu.file && !contextMenu.file.isDirectory && (
          <MenuItem
            onClick={() => {
              downloadFileWithAuth(contextMenu.file);
              handleContextMenuClose();
            }}
          >
            <Download sx={{ mr: 1 }} />
            Download
          </MenuItem>
        )}
      </Menu>

      {/* Rename Dialog */}
      <Dialog
        open={renameDialog.open}
        onClose={() =>
          setRenameDialog({ open: false, file: null, newName: "" })
        }
      >
        <DialogTitle>Rename File</DialogTitle>
        <DialogContent>
          <TextField
            autoFocus
            margin="dense"
            label="New Name"
            fullWidth
            variant="outlined"
            value={renameDialog.newName}
            onChange={(e) =>
              setRenameDialog({ ...renameDialog, newName: e.target.value })
            }
            onKeyPress={(e) => {
              if (e.key === "Enter") handleRenameSubmit();
            }}
          />
        </DialogContent>
        <DialogActions>
          <Button
            onClick={() =>
              setRenameDialog({ open: false, file: null, newName: "" })
            }
          >
            Cancel
          </Button>
          <Button onClick={handleRenameSubmit} variant="contained">
            Rename
          </Button>
        </DialogActions>
      </Dialog>

      {/* Delete Dialog */}
      <Dialog
        open={deleteDialog.open}
        onClose={() => setDeleteDialog({ open: false, files: [] })}
      >
        <DialogTitle>Delete Files</DialogTitle>
        <DialogContent>
          <Typography>
            Are you sure you want to delete {deleteDialog.files.length} file(s)?
          </Typography>
          <Box sx={{ mt: 1 }}>
            {deleteDialog.files.map((file, index) => (
              <Chip
                key={index}
                label={file.name}
                size="small"
                sx={{ mr: 1, mb: 1 }}
              />
            ))}
          </Box>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setDeleteDialog({ open: false, files: [] })}>
            Cancel
          </Button>
          <Button
            onClick={handleDeleteSubmit}
            variant="contained"
            color="error"
          >
            Delete
          </Button>
        </DialogActions>
      </Dialog>

      {/* New Folder Dialog */}
      <Dialog
        open={newFolderDialog.open}
        onClose={() => setNewFolderDialog({ open: false, folderName: "" })}
        maxWidth="sm"
        fullWidth
      >
        <DialogTitle>Create New Folder</DialogTitle>
        <DialogContent>
          <TextField
            autoFocus
            margin="dense"
            label="Folder Name"
            fullWidth
            variant="outlined"
            value={newFolderDialog.folderName}
            onChange={(e) =>
              setNewFolderDialog({
                ...newFolderDialog,
                folderName: e.target.value,
              })
            }
            placeholder="Enter folder name"
            onKeyPress={(e) => {
              if (e.key === "Enter") {
                handleCreateFolder();
              }
            }}
          />
        </DialogContent>
        <DialogActions>
          <Button
            onClick={() => setNewFolderDialog({ open: false, folderName: "" })}
          >
            Cancel
          </Button>
          <Button
            onClick={handleCreateFolder}
            variant="contained"
            disabled={loading || !newFolderDialog.folderName.trim()}
          >
            Create
          </Button>
        </DialogActions>
      </Dialog>
    </Box>
  );
};

export default FileBrowserComponent;
