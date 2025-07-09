# ESPWiFi OTA Update System

This project now includes Over-The-Air (OTA) update functionality for both firmware and filesystem updates.

## Features

- **Firmware Updates**: Upload new firmware binaries (.bin files) to update the device
- **Filesystem Updates**: Upload individual files to update the device filesystem
- **Web Interface**: User-friendly web interface for easy updates
- **Progress Tracking**: Real-time progress indicators during updates
- **Device Information**: Display current device status and capabilities

## Usage

### Web Interface (Recommended)

1. Connect to your ESPWiFi device's WiFi network
2. Open a web browser and navigate to: `http://[device-ip]/ota`
3. Use the web interface to:
   - View device information
   - Upload firmware updates
   - Upload filesystem files

### Direct API Endpoints

#### Firmware Update
```bash
curl -X POST -F "file=@firmware.bin" http://[device-ip]/update
```

#### Filesystem Update
```bash
curl -X POST -F "file=@myfile.txt" http://[device-ip]/fsupdate
```

#### Device Status
```bash
curl http://[device-ip]/ota/status
```

## Building for OTA

### PlatformIO

The project is configured with OTA partitions for ESP32 boards:

- **ESP32-C3**: Uses `default.csv` partition scheme
- **ESP32-S3**: Uses `default.csv` partition scheme
- **ESP32-CAM**: Uses default partitions with camera support

### Building Firmware

```bash
# Build for ESP32-C3
pio run -e esp32-c3

# Build for ESP32-S3
pio run -e esp32-s3

# Build for ESP32-CAM
pio run -e esp32-cam
```

### Uploading Filesystem

```bash
# Upload filesystem data
pio run -e esp32-c3 -t uploadfs
pio run -e esp32-s3 -t uploadfs
pio run -e esp32-cam -t uploadfs
```

## File Structure

```
data/
├── ota.html          # OTA web interface
├── index.html        # Main dashboard
├── config.json       # Configuration
└── ...               # Other web assets
```

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/ota` | GET | OTA web interface |
| `/ota/status` | GET | Device status and OTA info |
| `/update` | POST | Firmware update endpoint |
| `/fsupdate` | POST | Filesystem update endpoint |

## Security Considerations

- OTA updates are only available when connected to the device's network
- No authentication is currently implemented (consider adding for production use)
- Updates should be performed over a secure network connection

## Troubleshooting

### Update Fails
- Ensure the firmware binary is compatible with your device
- Check available flash space: `curl http://[device-ip]/ota/status`
- Verify network stability during upload

### Web Interface Not Loading
- Ensure the filesystem has been uploaded: `pio run -t uploadfs`
- Check that `ota.html` exists in the filesystem
- Verify the device is accessible on the network

### Device Won't Boot After Update
- If a firmware update fails, the device may not boot
- Use the serial monitor to check for error messages
- Consider using a development board with USB programming capability

## Development

### Adding New OTA Features

1. Update `ESPWiFi.h` with new method declarations
2. Implement functionality in `OTA.cpp`
3. Add web endpoints in `WebServer.cpp`
4. Update the web interface in `data/ota.html`

### Testing OTA Updates

1. Build and upload initial firmware
2. Make changes to the code
3. Build new firmware binary
4. Use OTA to upload the new firmware
5. Verify the device restarts with the new firmware

## Notes

- Firmware updates automatically restart the device
- Filesystem updates preserve existing files unless overwritten
- The OTA system logs all update activities
- Progress is reported every 10% during updates 