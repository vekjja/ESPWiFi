# File Browser Component

A React file browser component for the ESP WiFi dashboard that allows browsing, renaming, and deleting files on the ESP32's SD card and LittleFS file systems.

## Features

- **File System Support**: Browse both SD card and LittleFS file systems
- **File Operations**: Browse, rename, and delete files
- **File Upload**: Upload files to the ESP32
- **Modern UI**: Built with Material-UI and Chonky file browser
- **Real-time Updates**: Automatically refreshes after operations

## Dependencies

- `chonky` - Modern file browser component
- `chonky-icon-fontawesome` - FontAwesome icons for Chonky
- `@mui/material` - Material-UI components
- `@mui/icons-material` - Material-UI icons

## API Endpoints

The component communicates with the ESP32 backend through these endpoints:

- `GET /api/files?fs={sd|lfs}&path={path}` - List files in directory
- `POST /api/files/rename` - Rename a file
- `POST /api/files/delete` - Delete files
- `POST /api/files/upload` - Upload a file

## Usage

```jsx
import FileBrowserComponent from './components/FileBrowser';

<FileBrowserComponent
  config={localConfig}
  deviceOnline={deviceOnline}
/>
```

## Props

- `config` - Configuration object containing API URL
- `deviceOnline` - Boolean indicating if the ESP32 is online

## File Operations

### Browse Files
- Click on folders to navigate
- Use the file system toggle to switch between SD card and LittleFS
- Files are displayed in a list view with icons

### Rename Files
- Right-click on a file and select "Rename"
- Enter the new name in the dialog
- Click "Rename" to confirm

### Delete Files
- Right-click on files and select "Delete"
- Confirm deletion in the dialog
- Multiple files can be selected and deleted at once

### Upload Files
- Click the "Upload File" button
- Select a file from your computer
- The file will be uploaded to the current directory

## Error Handling

The component displays error messages for:
- Network connectivity issues
- File system unavailable
- Operation failures
- Invalid file names

## Styling

The component uses Material-UI's sx prop for styling and follows the existing dashboard theme. It includes:
- Dark theme support
- Responsive design
- Loading states
- Error alerts
