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

const FileBrowserComponent = ({ config, deviceOnline }) => {
  const theme = useTheme();
  const DeleteIcon = getDeleteIcon(theme);
  const EditIcon = getEditIcon(theme);

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
    <Box
      sx={{
        height: { xs: "100vh", sm: "calc(80vh - 120px)" }, // Full height on mobile, calculated on desktop
        display: "flex",
        flexDirection: "column",
        width: { xs: "100%", sm: "600px" },
        maxWidth: { xs: "100%", sm: "600px" },
        minWidth: { xs: "100%", sm: "600px" },
        mx: "auto", // Center the entire file browser
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
            <ToggleButton value="sd">
              <Storage sx={{ mr: 0.5 }} />
              <Box sx={{ display: { xs: "none", sm: "inline" } }}>SD Card</Box>
              <Box sx={{ display: { xs: "inline", sm: "none" } }}>SD</Box>
            </ToggleButton>
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
            disabled={loading}
            size="small"
            sx={{
              width: { xs: "100%", sm: "auto" },
              minWidth: "80px",
              px: 1,
              height: "32px", // Match toggle button height
            }}
          >
            <Box sx={{ display: { xs: "none", sm: "inline" } }}>Upload</Box>
            <Box sx={{ display: { xs: "inline", sm: "none" } }}>Upload</Box>
            <input type="file" hidden onChange={handleUpload} />
          </Button>
        </Box>

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
                    file.isDirectory
                      ? "Folder"
                      : `${(file.size / 1024).toFixed(1)} KB`
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
