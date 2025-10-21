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
import {
  FolderOpen,
  Storage,
  Refresh,
  Upload,
  Delete,
  Edit,
  Download,
  Folder,
  InsertDriveFile,
  MoreVert,
  Home,
} from "@mui/icons-material";

const FileBrowserComponent = ({ config, deviceOnline }) => {
  const [files, setFiles] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState(null);
  const [currentPath, setCurrentPath] = useState("/");
  const [fileSystem, setFileSystem] = useState("sd"); // 'sd' or 'lfs'
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

  // Fetch files from ESP32
  const fetchFiles = useCallback(
    async (path = currentPath, fs = fileSystem) => {
      setLoading(true);
      setError(null);

      try {
        const response = await fetch(
          `${apiURL}/api/files?fs=${fs}&path=${encodeURIComponent(path)}`
        );
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const data = await response.json();
        setFiles(data.files || []);
        setCurrentPath(path);
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
    [apiURL, currentPath, fileSystem]
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
      // Download file
      window.open(`${apiURL}/${fileSystem}${file.path}`, "_blank");
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
        {
          method: "POST",
        }
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
          {
            method: "POST",
          }
        );

        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
      }

      setDeleteDialog({ open: false, files: [] });
      fetchFiles(currentPath, fileSystem);
    } catch (err) {
      if (err.name === "TypeError" && err.message.includes("fetch")) {
        setError("Device is offline. Cannot delete files.");
      } else {
        setError(`Failed to delete files: ${err.message}`);
      }
    }
  };

  // Upload file
  const handleUpload = (event) => {
    const file = event.target.files[0];
    if (!file) return;

    // Create FormData with file and URL parameters for fs and path
    const formData = new FormData();
    formData.append("file", file);

    // Add fs and path as URL parameters like OTA does
    const url = `${apiURL}/api/files/upload?fs=${encodeURIComponent(
      fileSystem
    )}&path=${encodeURIComponent(currentPath)}`;

    fetch(url, {
      method: "POST",
      body: formData,
    })
      .then((response) => {
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }
        fetchFiles(currentPath, fileSystem);
      })
      .catch((err) => {
        setError(`Failed to upload file: ${err.message}`);
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
      fetchFiles();
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
    <Box sx={{ height: "100%", display: "flex", flexDirection: "column" }}>
      {/* Header */}
      <Paper sx={{ p: 2, mb: 2 }}>
        <Box
          sx={{
            display: "flex",
            alignItems: "center",
            justifyContent: "space-between",
            mb: 2,
          }}
        >
          <Typography
            variant="h5"
            sx={{ display: "flex", alignItems: "center", gap: 1 }}
          >
            <FolderOpen color="primary" />
          </Typography>
          <Box sx={{ display: "flex", gap: 1 }}>
            <ToggleButtonGroup
              value={fileSystem}
              exclusive
              onChange={handleFileSystemChange}
              size="small"
            >
              <ToggleButton value="sd">
                <Storage sx={{ mr: 1 }} />
                SD Card
              </ToggleButton>
              <ToggleButton value="lfs">
                <Storage sx={{ mr: 1 }} />
                Internal
              </ToggleButton>
            </ToggleButtonGroup>
            <Button
              variant="outlined"
              startIcon={<Refresh />}
              onClick={() => fetchFiles(currentPath, fileSystem)}
              disabled={loading}
            >
              Refresh
            </Button>
          </Box>
        </Box>

        {/* Breadcrumbs */}
        <Breadcrumbs sx={{ mb: 2 }}>{generateBreadcrumbs()}</Breadcrumbs>

        {/* Error Display */}
        {error && (
          <Alert severity="error" sx={{ mb: 2 }} onClose={() => setError(null)}>
            {error}
          </Alert>
        )}
      </Paper>

      {/* File List */}
      <Paper sx={{ flex: 1, overflow: "hidden" }}>
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
          <List sx={{ height: "100%", overflow: "auto" }}>
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
                    file.isDirectory
                      ? "Folder"
                      : `${(file.size / 1024).toFixed(1)} KB`
                  }
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

      {/* Upload Button */}
      <Box sx={{ mt: 2, display: "flex", justifyContent: "center" }}>
        <Button
          variant="contained"
          component="label"
          startIcon={<Upload />}
          disabled={loading}
        >
          Upload File
          <input type="file" hidden onChange={handleUpload} />
        </Button>
      </Box>

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
          <Edit sx={{ mr: 1 }} />
          Rename
        </MenuItem>
        <MenuItem onClick={handleDelete} sx={{ color: "error.main" }}>
          <Delete sx={{ mr: 1 }} />
          Delete
        </MenuItem>
        {contextMenu.file && !contextMenu.file.isDirectory && (
          <MenuItem
            onClick={() => {
              window.open(
                `${apiURL}/${fileSystem}${contextMenu.file.path}`,
                "_blank"
              );
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
    </Box>
  );
};

export default FileBrowserComponent;
