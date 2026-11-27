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
import { getFetchOptions, buildApiUrl, buildWebSocketUrl } from "../../../utils/api";
import StorageIcon from "@mui/icons-material/Storage";
import UploadIcon from "@mui/icons-material/Upload";
import DownloadIcon from "@mui/icons-material/Download";
import FolderIcon from "@mui/icons-material/Folder";
import InsertDriveFileIcon from "@mui/icons-material/InsertDriveFile";
import MoreVertIcon from "@mui/icons-material/MoreVert";
import HomeIcon from "@mui/icons-material/Home";
import RefreshIcon from "@mui/icons-material/Refresh";
import { useTheme } from "@mui/material";
import { getDeleteIcon, getEditIcon } from "../../../utils/theme";

// Helper function to format bytes
const fileTransferError = (code) => {
  switch (code) {
    case "protected":
      return "That file is protected";
    case "sd_not_mounted":
      return "SD card is not mounted";
    case "lfs_not_mounted":
      return "Filesystem is not mounted";
    case "file_open_failed":
      return "Could not open the file";
    case "write_failed":
      return "Write failed";
    case "invalid_name":
      return "Invalid file name";
    case "invalid_path":
      return "Invalid path";
    case "is_directory":
      return "That path is a directory";
    case "no_active_upload":
      return "Upload was interrupted";
    case "no_active_download":
      return "Download was interrupted";
    case "unknown_cmd":
      return "Device firmware needs to be redeployed";
    default:
      return code || "Transfer failed";
  }
};

const mimeForFile = (name) => {
  const lower = (name || "").toLowerCase();
  if (lower.endsWith(".txt") || lower.endsWith(".log") || lower.endsWith(".csv")) {
    return "text/plain";
  }
  if (lower.endsWith(".json")) return "application/json";
  if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html";
  if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
  if (lower.endsWith(".png")) return "image/png";
  if (lower.endsWith(".gif")) return "image/gif";
  if (lower.endsWith(".webp")) return "image/webp";
  if (lower.endsWith(".svg")) return "image/svg+xml";
  if (lower.endsWith(".pdf")) return "application/pdf";
  if (lower.endsWith(".mp3")) return "audio/mpeg";
  if (lower.endsWith(".wav")) return "audio/wav";
  if (lower.endsWith(".mp4")) return "video/mp4";
  return "application/octet-stream";
};

const formatBytes = (bytes) => {
  if (bytes === 0) return "0 B";
  const k = 1024;
  const sizes = ["B", "KB", "MB", "GB"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i];
};

const FileBrowserComponent = ({ config, deviceInfo, deviceOnline, controlWs }) => {
  const theme = useTheme();
  const DeleteIcon = getDeleteIcon(theme);
  const EditIcon = getEditIcon(theme);

  const sdAvailable =
    config?.sd?.initialized === true ||
    (typeof deviceInfo?.sd_total === "number" && deviceInfo.sd_total > 0);

  // Check if we should use WebSocket (when controlWs is available and open)
  const useWebSocket = controlWs && controlWs.readyState === WebSocket.OPEN;

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
  const mediaXferRef = useRef(null);
  const listSeqRef = useRef(0);
  const openedRef = useRef(false);
  const promotedSdRef = useRef(false);

  // Helper to send WebSocket command and wait for response
  const sendWsCommand = useCallback(
    (cmd) => {
      if (!controlWs || controlWs.readyState !== WebSocket.OPEN) {
        return Promise.reject(new Error("WebSocket not connected"));
      }

      if (typeof controlWs.sendCommand !== "function") {
        return Promise.reject(
          new Error("WebSocket sendCommand method not available")
        );
      }

      return controlWs.sendCommand(cmd, 10000);
    },
    [controlWs]
  );

  // Helper function to reset download state
  const resetDownloadState = useCallback(() => {
    setIsDownloading(false);
    setDownloadProgress(0);
    setDownloadingFileName("");
    setDownloadIndeterminate(false);
    mediaXferRef.current = null;
  }, []);

  // Cleanup function for aborting requests
  useEffect(() => {
    return () => {
      if (abortControllerRef.current) {
        abortControllerRef.current.abort();
      }
      if (mediaXferRef.current) {
        mediaXferRef.current.close();
        mediaXferRef.current = null;
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

  const openMediaSocket = useCallback(() => {
    const mdnsHostname = config?.hostname || config?.deviceName || null;
    const url = buildWebSocketUrl("/ws/media", mdnsHostname);
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(url);
      ws.binaryType = "arraybuffer";
      let settled = false;
      const timer = setTimeout(() => {
        if (settled) return;
        settled = true;
        try {
          ws.close();
        } catch {
          // ignore
        }
        reject(new Error("Cannot connect to device"));
      }, 8000);
      ws.onopen = () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        resolve(ws);
      };
      ws.onerror = () => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        reject(new Error("Cannot connect to device"));
      };
    });
  }, [config]);

  const pullFile = useCallback(
    (file, onProgress) => {
      return openMediaSocket().then(
        (ws) =>
          new Promise((resolve, reject) => {
            if (mediaXferRef.current && mediaXferRef.current !== ws) {
              try {
                mediaXferRef.current.close();
              } catch {
                // ignore
              }
            }
            mediaXferRef.current = ws;
            const parts = [];
            let received = 0;
            let total = file?.size || 0;
            let eof = false;
            let settled = false;
            let idle;
            const finish = (err, blob) => {
              if (settled) return;
              settled = true;
              clearTimeout(idle);
              if (mediaXferRef.current === ws) mediaXferRef.current = null;
              try {
                ws.close();
              } catch {
                // ignore
              }
              if (err) reject(err);
              else resolve(blob);
            };
            const bump = () => {
              clearTimeout(idle);
              idle = setTimeout(
                () => finish(new Error("Request timed out")),
                30000
              );
            };
            bump();
            ws.onerror = () => finish(new Error("Network error"));
            ws.onclose = () => finish(new Error("Connection closed"));
            ws.onmessage = (ev) => {
              bump();
              if (ev.data instanceof ArrayBuffer) {
                parts.push(ev.data);
                received += ev.data.byteLength;
                if (total > 0 && onProgress) {
                  onProgress(
                    Math.min(100, Math.round((received / total) * 100))
                  );
                }
                if (eof) {
                  finish(
                    null,
                    new Blob(parts, { type: "application/octet-stream" })
                  );
                } else if (ws.readyState === WebSocket.OPEN) {
                  ws.send(JSON.stringify({ cmd: "file_down_next" }));
                }
                return;
              }
              if (typeof ev.data !== "string") return;
              let msg;
              try {
                msg = JSON.parse(ev.data);
              } catch {
                return;
              }
              if (msg.ok === false) {
                finish(new Error(fileTransferError(msg.error)));
                return;
              }
              if (msg.type === "file_down_start") {
                total = msg.size || total || 0;
                if (ws.readyState === WebSocket.OPEN) {
                  ws.send(JSON.stringify({ cmd: "file_down_next" }));
                }
                return;
              }
              if (msg.type === "file_chunk" && (msg.len || 0) === 0) {
                finish(
                  null,
                  new Blob(parts, { type: "application/octet-stream" })
                );
              } else if (msg.type === "file_chunk") {
                eof = !!msg.eof;
              }
            };
            ws.send(
              JSON.stringify({
                cmd: "file_down_start",
                fs: fileSystem,
                path: file.path,
              })
            );
          })
      );
    },
    [openMediaSocket, fileSystem]
  );

  const uploadViaMedia = useCallback(
    (file) => {
      return openMediaSocket().then(
        (ws) =>
          new Promise((resolve, reject) => {
            if (mediaXferRef.current && mediaXferRef.current !== ws) {
              try {
                mediaXferRef.current.close();
              } catch {
                // ignore
              }
            }
            mediaXferRef.current = ws;
            let settled = false;
            let idle;
            const finish = (err) => {
              if (settled) return;
              settled = true;
              clearTimeout(idle);
              if (mediaXferRef.current === ws) mediaXferRef.current = null;
              try {
                ws.close();
              } catch {
                // ignore
              }
              if (err) reject(err);
              else resolve();
            };
            const bump = () => {
              clearTimeout(idle);
              idle = setTimeout(
                () => finish(new Error("Request timed out")),
                30000
              );
            };
            const sendSlice = (offset, chunkSize) => {
              const size = chunkSize || 8192;
              const end = Math.min(offset + size, file.size);
              if (end <= offset) return;
              file
                .slice(offset, end)
                .arrayBuffer()
                .then((buf) => {
                  if (settled || ws.readyState !== WebSocket.OPEN) return;
                  ws.send(buf);
                })
                .catch((err) => finish(err));
            };
            bump();
            ws.onerror = () => finish(new Error("Network error"));
            ws.onclose = () => finish(new Error("Connection closed"));
            ws.onmessage = (ev) => {
              bump();
              if (typeof ev.data !== "string") return;
              let msg;
              try {
                msg = JSON.parse(ev.data);
              } catch {
                return;
              }
              if (msg.ok === false) {
                finish(new Error(fileTransferError(msg.error)));
                return;
              }
              if (msg.type === "file_up_start") {
                if (file.size > 0) sendSlice(0, msg.chunkSize || 8192);
                return;
              }
              if (msg.type === "file_up_ack") {
                const offset = msg.offset || 0;
                if (file.size > 0) {
                  setUploadProgress(
                    Math.min(100, Math.round((offset / file.size) * 100))
                  );
                }
                sendSlice(offset, msg.chunkSize || 8192);
                return;
              }
              if (msg.type === "file_up_done") {
                setUploadProgress(100);
                finish(null);
              }
            };
            ws.send(
              JSON.stringify({
                cmd: "file_up_start",
                fs: fileSystem,
                path: currentPath,
                name: file.name,
                size: file.size,
              })
            );
          })
      );
    },
    [openMediaSocket, fileSystem, currentPath]
  );

  // Open a file by pulling it over the media socket, then showing the blob.
  // The blank tab is opened in this click so the browser still treats it as
  // a user gesture after the transfer finishes.
  const openFileWithAuth = useCallback(
    (file) => {
      const tab = window.open("about:blank", "_blank");
      if (!tab) {
        setError(
          "Popup blocked. Please allow popups for this site, or try downloading the file instead."
        );
        return;
      }
      try {
        tab.document.title = file?.name || "File";
      } catch {
        // ignore
      }
      setIsDownloading(true);
      setDownloadProgress(0);
      setDownloadingFileName(file.name);
      setDownloadIndeterminate(false);
      setError(null);
      pullFile(file, (pct) => {
        setDownloadProgress(pct);
        setDownloadIndeterminate(false);
      })
        .then((blob) => {
          const typed = new Blob([blob], { type: mimeForFile(file?.name) });
          const url = URL.createObjectURL(typed);
          tab.location.href = url;
          setTimeout(() => URL.revokeObjectURL(url), 60000);
          setInfo(`Opening ${file.name}...`);
        })
        .catch((err) => {
          try {
            tab.close();
          } catch {
            // ignore
          }
          setError(`Failed to open file: ${err.message}`);
        })
        .finally(() => {
          resetDownloadState();
        });
    },
    [pullFile, resetDownloadState]
  );

  const downloadFileWithAuth = useCallback(
    async (file) => {
      setIsDownloading(true);
      setDownloadProgress(0);
      setDownloadingFileName(file.name);
      setDownloadIndeterminate(false);
      setError(null);
      try {
        const blob = await pullFile(file, (pct) => {
          setDownloadProgress(pct);
          setDownloadIndeterminate(false);
        });
        const downloadBlob = new Blob([blob], {
          type: "application/octet-stream",
        });
        const blobUrl = URL.createObjectURL(downloadBlob);
        const a = document.createElement("a");
        a.href = blobUrl;
        a.download = file?.name || "download";
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(() => URL.revokeObjectURL(blobUrl), 60000);
        setInfo(`Downloaded ${file.name}`);
      } catch (err) {
        setError(`Failed to download file: ${err.message}`);
        throw err;
      } finally {
        resetDownloadState();
      }
    },
    [pullFile, resetDownloadState]
  );

  // Fetch storage information
  const fetchStorageInfo = useCallback(
    async (fs = fileSystem) => {
      setStorageInfo((prev) => ({ ...prev, loading: true }));

      try {
        if (useWebSocket) {
          // Use WebSocket
          const response = await sendWsCommand({
            cmd: "get_storage",
            fs: fs,
          });
          setStorageInfo({
            total: response.total || 0,
            used: response.used || 0,
            free: response.free || 0,
            loading: false,
          });
        } else {
          // Use HTTP
          const mdnsHostname = config?.hostname || config?.deviceName;
          const storageUrl = buildApiUrl(
            `/api/storage?fs=${encodeURIComponent(fs)}`,
            mdnsHostname
          );
          const response = await fetchWithRetry(storageUrl);
          const data = await response.json();
          setStorageInfo({
            total: data.total || 0,
            used: data.used || 0,
            free: data.free || 0,
            loading: false,
          });
        }
      } catch (err) {
        console.error(`Failed to fetch storage info: ${err.message}`);
        setStorageInfo((prev) => ({ ...prev, loading: false }));
      }
    },
    [fileSystem, fetchWithRetry, useWebSocket, sendWsCommand, config]
  );

  // Fetch files with retry and better error handling
  const fetchFiles = useCallback(
    async (path = currentPath, fs = fileSystem) => {
      const seq = (listSeqRef.current += 1);
      setLoading(true);
      setError(null);

      try {
        if (useWebSocket) {
          // Use WebSocket
          const response = await sendWsCommand({
            cmd: "list_files",
            fs: fs,
            path: path,
          });
          if (seq !== listSeqRef.current) return;
          setFiles(response.files || []);
          setCurrentPath(path);
          setFileSystem(fs);
          fetchStorageInfo(fs);
        } else {
          // Use HTTP
          const mdnsHostname = config?.hostname || config?.deviceName;
          const filesUrl = buildApiUrl(
            `/api/files?fs=${fs}&path=${encodeURIComponent(path)}`,
            mdnsHostname
          );
          const response = await fetchWithRetry(filesUrl);

          const data = await response.json();
          if (seq !== listSeqRef.current) return;
          setFiles(data.files || []);
          setCurrentPath(path);
          setFileSystem(fs);
          fetchStorageInfo(fs);
        }
      } catch (err) {
        if (seq !== listSeqRef.current) return;
        let errorMsg = "Failed to load files";
        if (
          err.message === "Request timed out" ||
          err.message.includes("timed out")
        ) {
          errorMsg = "Device is busy. Please wait a moment and try again.";
        } else if (
          err.message.includes("NetworkError") ||
          err.message.includes("fetch") ||
          err.message.includes("WebSocket")
        ) {
          errorMsg = "Cannot connect to device. Check your connection.";
        } else {
          errorMsg = `Failed to load files: ${err.message}`;
        }
        setError(errorMsg);
        console.error("Error fetching files:", err);
      } finally {
        if (seq === listSeqRef.current) setLoading(false);
      }
    },
    [fetchStorageInfo, fetchWithRetry, useWebSocket, sendWsCommand, config]
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
      if (useWebSocket) {
        // Use WebSocket
        await sendWsCommand({
          cmd: "mkdir",
          fs: fileSystem,
          path: currentPath,
          name: newFolderDialog.folderName.trim(),
        });
      } else {
        // Use HTTP
        const mdnsHostname = config?.hostname || config?.deviceName;
        const mkdirUrl = buildApiUrl("/api/files/mkdir", mdnsHostname);
        await fetchWithRetry(mkdirUrl, {
          method: "POST",
          body: JSON.stringify({
            fs: fileSystem,
            path: currentPath,
            name: newFolderDialog.folderName.trim(),
          }),
        });
      }

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
      if (useWebSocket) {
        // Use WebSocket
        await sendWsCommand({
          cmd: "rename_file",
          fs: fileSystem,
          oldPath: renameDialog.file.path,
          newName: renameDialog.newName.trim(),
        });
      } else {
        // Use HTTP
        const mdnsHostname = config?.hostname || config?.deviceName;
        const renameUrl = buildApiUrl(
          `/api/files/rename?fs=${encodeURIComponent(
            fileSystem
          )}&oldPath=${encodeURIComponent(
            renameDialog.file.path
          )}&newName=${encodeURIComponent(renameDialog.newName.trim())}`,
          mdnsHostname
        );
        await fetchWithRetry(renameUrl, { method: "POST" });
      }

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
        if (useWebSocket) {
          // Use WebSocket
          await sendWsCommand({
            cmd: "delete_file",
            fs: fileSystem,
            path: file.path,
          });
        } else {
          // Use HTTP
          const mdnsHostname = config?.hostname || config?.deviceName;
          const deleteUrl = buildApiUrl(
            `/api/files/delete?fs=${encodeURIComponent(
              fileSystem
            )}&path=${encodeURIComponent(file.path)}`,
            mdnsHostname
          );
          await fetchWithRetry(deleteUrl, { method: "POST" });
        }
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

  const handleUpload = (event) => {
    const file = event.target.files[0];
    event.target.value = "";
    if (!file) return;

    if (storageInfo.total > 0 && file.size > storageInfo.free) {
      setError(
        `File too large: ${formatBytes(file.size)}. Only ${formatBytes(
          storageInfo.free
        )} available.`
      );
      return;
    }

    setUploadProgress(0);
    setIsUploading(true);
    setError(null);
    uploadViaMedia(file)
      .then(() => {
        setInfo("File uploaded successfully");
        setTimeout(() => {
          setIsUploading(false);
          fetchFiles(currentPath, fileSystem);
        }, 300);
      })
      .catch((err) => {
        setError(`Upload failed: ${err.message}`);
        setIsUploading(false);
        setUploadProgress(0);
      });
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
        <HomeIcon fontSize="small" />
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

  // Load the root listing when the browser opens. Later device-info
  // refreshes must not send the view back to root.
  useEffect(() => {
    if (!deviceOnline) {
      openedRef.current = false;
      promotedSdRef.current = false;
      return;
    }
    if (!config) return;
    if (!openedRef.current) {
      openedRef.current = true;
      if (sdAvailable) promotedSdRef.current = true;
      fetchFiles("/", sdAvailable ? "sd" : "lfs");
      return;
    }
    if (
      sdAvailable &&
      !promotedSdRef.current &&
      currentPath === "/" &&
      fileSystem === "lfs"
    ) {
      promotedSdRef.current = true;
      fetchFiles("/", "sd");
    }
  }, [deviceOnline, config, sdAvailable, fetchFiles, currentPath, fileSystem]);

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
            {sdAvailable && (
              <ToggleButton value="sd">
                <StorageIcon sx={{ mr: 0.5 }} />
                <Box sx={{ display: { xs: "none", sm: "inline" } }}>
                  SD Card
                </Box>
                <Box sx={{ display: { xs: "inline", sm: "none" } }}>SD</Box>
              </ToggleButton>
            )}
            <ToggleButton value="lfs">
              <StorageIcon sx={{ mr: 0.5 }} />
              <Box sx={{ display: { xs: "none", sm: "inline" } }}>Device</Box>
              <Box sx={{ display: { xs: "inline", sm: "none" } }}>Int</Box>
            </ToggleButton>
          </ToggleButtonGroup>

          <Button
            variant="contained"
            component="label"
            startIcon={<UploadIcon />}
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
            startIcon={<FolderIcon />}
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
            <RefreshIcon />
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
                  <FolderIcon color="primary" />
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
                    <FolderIcon color="primary" />
                  ) : (
                    <InsertDriveFileIcon color="action" />
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
                    <MoreVertIcon />
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
            <DownloadIcon sx={{ mr: 1 }} />
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
