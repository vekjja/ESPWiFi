import React, { useState, useEffect, useCallback } from "react";
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

const FileBrowserComponent = ({ config, deviceOnline }) => {
  const theme = useTheme();
  const DeleteIcon = getDeleteIcon(theme);
  const EditIcon = getEditIcon(theme);

  const [files, setFiles] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [info, setInfo] = useState(null);
  const [newFolderDialog, setNewFolderDialog] = useState({
    open: false,
    folderName: "",
  });
  const [currentPath, setCurrentPath] = useState("/");
  const [fileSystem, setFileSystem] = useState("lfs"); // 'sd' or 'lfs'
  const [uploadProgress, setUploadProgress] = useState(0);
  const [isUploading, setIsUploading] = useState(false);
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

  const apiURL = config?.apiURL || "";

  const openFileWithAuth = useCallback(
    async (file) => {
      // window.open can't attach headers; fetch with auth then open a blob URL
      const newTab = window.open("", "_blank");
      if (newTab) {
        newTab.document.title = file?.name || "File";
        newTab.document.body.style.background = "#111";
        newTab.document.body.style.color = "#fff";
        newTab.document.body.style.fontFamily = "system-ui, sans-serif";
        newTab.document.body.style.padding = "16px";
        newTab.document.body.textContent = "Loading…";
      }

      const fileUrl = `${apiURL}/${fileSystem}${file.path}`;
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 15000);

      try {
        const response = await fetch(
          fileUrl,
          getFetchOptions({ method: "GET", signal: controller.signal })
        );

        clearTimeout(timeoutId);

        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const contentType =
          response.headers.get("content-type") || "application/octet-stream";
        const blob = await response.blob();
        const blobUrl = URL.createObjectURL(
          new Blob([blob], { type: contentType })
        );

        if (newTab) {
          newTab.location.href = blobUrl;
        } else {
          window.open(blobUrl, "_blank");
        }

        setTimeout(() => URL.revokeObjectURL(blobUrl), 60_000);
      } catch (err) {
        clearTimeout(timeoutId);
        const msg =
          err?.name === "AbortError"
            ? "Request timed out - device may be offline"
            : `Failed to open file: ${err.message || err}`;
        setError(msg);
        if (newTab) {
          newTab.document.body.textContent = msg;
        }
      }
    },
    [apiURL, fileSystem]
  );

  const downloadFileWithAuth = useCallback(
    async (file) => {
      const fileUrl = `${apiURL}/${fileSystem}${file.path}`;
      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 15000);

      try {
        const response = await fetch(
          fileUrl,
          getFetchOptions({ method: "GET", signal: controller.signal })
        );

        clearTimeout(timeoutId);

        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const contentType =
          response.headers.get("content-type") || "application/octet-stream";
        const blob = await response.blob();
        const blobUrl = URL.createObjectURL(
          new Blob([blob], { type: contentType })
        );

        const a = document.createElement("a");
        a.href = blobUrl;
        a.download = file?.name || "download";
        document.body.appendChild(a);
        a.click();
        a.remove();

        setTimeout(() => URL.revokeObjectURL(blobUrl), 60_000);
      } catch (err) {
        clearTimeout(timeoutId);
        const msg =
          err?.name === "AbortError"
            ? "Request timed out - device may be offline"
            : `Failed to download file: ${err.message || err}`;
        setError(msg);
      }
    },
    [apiURL, fileSystem]
  );

  // Fetch files from ESP32
  // Fetch storage information
  const fetchStorageInfo = useCallback(
    async (fs = fileSystem) => {
      setStorageInfo((prev) => ({ ...prev, loading: true }));

      try {
        const response = await fetch(
          `${apiURL}/api/storage?fs=${encodeURIComponent(fs)}`,
          getFetchOptions()
        );
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

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
    [fileSystem, apiURL]
  );

  const fetchFiles = useCallback(
    async (path = currentPath, fs = fileSystem) => {
      setLoading(true);
      setError(null);

      try {
        const response = await fetch(
          `${apiURL}/api/files?fs=${fs}&path=${encodeURIComponent(path)}`,
          getFetchOptions()
        );
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const data = await response.json();
        setFiles(data.files || []);
        setCurrentPath(path);
        setFileSystem(fs);
        // Fetch storage info when filesystem changes
        fetchStorageInfo(fs);
      } catch (err) {
        if (err.name === "TypeError" && err.message.includes("fetch")) {
          setError(
            "Device is offline. Please check your connection and try again."
          );
        } else {
          setError(`Failed to load files: ${err.message}`);
        }
        console.error("Error fetching files:", err);
      } finally {
        setLoading(false);
      }
    },
    [apiURL, fetchStorageInfo]
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
      const response = await fetch(
        `${apiURL}/api/files/mkdir`,
        getFetchOptions({
          method: "POST",
          body: JSON.stringify({
            fs: fileSystem,
            path: currentPath,
            name: newFolderDialog.folderName.trim(),
          }),
        })
      );

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      // Close dialog and refresh files
      setNewFolderDialog({ open: false, folderName: "" });
      fetchFiles(currentPath, fileSystem);
      setInfo(`Folder "${newFolderDialog.folderName}" created successfully`);
    } catch (err) {
      setError(`Failed to create folder: ${err.message}`);
    } finally {
      setLoading(false);
    }
  };

  // Handle context menu
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

  // Handle file actions
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
      const response = await fetch(
        `${apiURL}/api/files/rename?fs=${encodeURIComponent(
          fileSystem
        )}&oldPath=${encodeURIComponent(
          renameDialog.file.path
        )}&newName=${encodeURIComponent(renameDialog.newName.trim())}`,
        getFetchOptions({ method: "POST" })
      );

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
      }

      setRenameDialog({ open: false, file: null, newName: "" });
      fetchFiles(currentPath, fileSystem);
    } catch (err) {
      if (err.name === "TypeError" && err.message.includes("fetch")) {
        setError("Device is offline. Cannot rename file.");
      } else {
        setError(`Failed to rename file: ${err.message}`);
      }
    }
  };

  // Delete files
  const handleDeleteSubmit = async () => {
    try {
      // Delete files one by one
      for (const file of deleteDialog.files) {
        const response = await fetch(
          `${apiURL}/api/files/delete?fs=${encodeURIComponent(
            fileSystem
          )}&path=${encodeURIComponent(file.path)}`,
          getFetchOptions({ method: "POST" })
        );

        if (!response.ok) {
          // Close dialog if 403 (Forbidden)
          if (response.status === 403) {
            // Close dialog immediately
            setDeleteDialog((prev) => ({ ...prev, open: false, files: [] }));
            setError(
              `Failed to delete files: HTTP ${response.status} - ${response.statusText}`
            );
            return;
          }
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
      }

      setDeleteDialog({ open: false, files: [] });
      fetchFiles(currentPath, fileSystem);
    } catch (err) {
      // Check if error is related to 403 Forbidden
      if (err.message && err.message.includes("403")) {
        setDeleteDialog((prev) => ({ ...prev, open: false, files: [] }));
        setError(`Failed to delete files: ${err.message}`);
      } else if (err.name === "TypeError" && err.message.includes("fetch")) {
        setError("Device is offline. Cannot delete files.");
      } else {
        setError(`Failed to delete files: ${err.message}`);
      }
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
      event.target.value = ""; // Reset file input
      return;
    }

    // Reset progress and start uploading
    setUploadProgress(0);
    setIsUploading(true);
    setError(null);

    // Create FormData with file and URL parameters for fs and path
    const formData = new FormData();
    formData.append("file", file);

    // Add fs and path as URL parameters like OTA does
    const url = `${apiURL}/api/files/upload?fs=${encodeURIComponent(
      fileSystem
    )}&path=${encodeURIComponent(currentPath)}`;

    // Use XMLHttpRequest for progress tracking
    const xhr = new XMLHttpRequest();

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
        setIsUploading(false);
        fetchFiles(currentPath, fileSystem);
        // Reset file input
        event.target.value = "";

        // Log filename sanitization if needed
        const originalName = file.name;
        let sanitizedName = originalName
          .replace(/ /g, "_")
          .replace(/[^a-zA-Z0-9._-]/g, "_")
          .replace(/_+/g, "_")
          .replace(/^_|_$/g, "");

        // Check filename length limit (LittleFS typically has 31 char limit)
        const maxFilenameLength = 31;
        if (sanitizedName.length > maxFilenameLength) {
          // Try to preserve file extension
          const lastDot = sanitizedName.lastIndexOf(".");
          let extension = "";
          let baseName = sanitizedName;

          if (lastDot > 0 && lastDot > sanitizedName.length - 6) {
            // Extension is reasonable length
            extension = sanitizedName.substring(lastDot);
            baseName = sanitizedName.substring(0, lastDot);
          }

          // Truncate base name to fit extension and add unique suffix
          const maxBaseLength = maxFilenameLength - extension.length - 4; // Reserve 4 chars for unique suffix
          if (maxBaseLength > 0) {
            const uniqueSuffix = Math.random().toString(36).substring(2, 6); // 4-char random suffix
            sanitizedName =
              baseName.substring(0, maxBaseLength) +
              "_" +
              uniqueSuffix +
              extension;
          } else {
            // If no room for extension, just truncate and add suffix
            const uniqueSuffix = Math.random().toString(36).substring(2, 6);
            sanitizedName =
              sanitizedName.substring(0, maxFilenameLength - 5) +
              "_" +
              uniqueSuffix;
          }
        }

        if (originalName !== sanitizedName) {
          // Show info message about filename sanitization
          setInfo(`Filename sanitized: "${originalName}" → "${sanitizedName}"`);
        }
      } else {
        setError(`Upload failed: HTTP ${xhr.status} - ${xhr.statusText}`);
        setIsUploading(false);
        setUploadProgress(0);
      }
    });

    // Handle upload errors
    xhr.addEventListener("error", () => {
      setError("Upload failed: Network error");
      setIsUploading(false);
      setUploadProgress(0);
    });

    // Handle upload abort
    xhr.addEventListener("abort", () => {
      setIsUploading(false);
      setUploadProgress(0);
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
      breadcrumbs.push(
        <Link
          key={index}
          component="button"
          onClick={() => fetchFiles(currentBreadcrumbPath, fileSystem)}
          underline="none"
        >
          {part}
        </Link>
      );
    });

    return breadcrumbs;
  };

  // Initialize
  useEffect(() => {
    if (config && deviceOnline) {
      const initialFs = config?.sd?.initialized === true ? "sd" : "lfs";
      fetchFiles("/", initialFs);
    }
  }, [config, deviceOnline]);

  if (!deviceOnline) {
    return (
      <Paper sx={{ p: 3, textAlign: "center" }}>
        <Alert severity="error">
          Device is offline. Cannot access file system.
        </Alert>
      </Paper>
    );
  }

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
            display: "flex",
            alignItems: "center",
            justifyContent: "center",
            mb: 1,
            flexDirection: { xs: "column", sm: "row" },
            gap: { xs: 2, sm: 2 },
            flexWrap: "wrap",
          }}
        >
          <ToggleButtonGroup
            value={fileSystem}
            exclusive
            onChange={handleFileSystemChange}
            size="small"
            sx={{
              width: { xs: "100%", sm: "auto" },
              "& .MuiToggleButton-root": {
                minWidth: "100px",
                height: "32px",
                flex: { xs: 1, sm: "none" },
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
            disabled={loading || isUploading}
            size="small"
            sx={{
              width: { xs: "100%", sm: "auto" },
              minWidth: "80px",
              px: 1,
              height: "32px", // Match toggle button height
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
            disabled={loading || isUploading}
            size="small"
            sx={{
              width: { xs: "100%", sm: "auto" },
              minWidth: "100px",
              px: 1,
              height: "32px", // Match toggle button height
            }}
          >
            <Box sx={{ display: { xs: "none", sm: "inline" } }}>New Folder</Box>
            <Box sx={{ display: { xs: "inline", sm: "none" } }}>Folder</Box>
          </Button>
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
          }}
        >
          {generateBreadcrumbs()}
        </Breadcrumbs>

        {/* Info Display */}
        {info && (
          <Alert
            severity="info"
            sx={{
              mb: 2,
              backgroundColor: theme.palette.primary.main + "15", // 15% opacity
              color: theme.palette.primary.main,
              border: `1px solid ${theme.palette.primary.main}30`, // 30% opacity
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
