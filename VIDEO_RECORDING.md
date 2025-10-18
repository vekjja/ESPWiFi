# Video Recording Feature

This ESP WiFi library now supports video recording functionality that captures video from the ESP32-CAM and saves it to the filesystem as MJPEG format.

## Features

- **MJPEG Format**: Records video in Motion JPEG format for broad compatibility
- **Configurable Frame Rate**: Default 10 FPS, adjustable via code
- **Automatic Duration Limits**: Auto-stops recording after 60 seconds to prevent storage overflow
- **Web API Control**: Start/stop recording via HTTP endpoints
- **File System Integration**: Works with both LittleFS and SD card storage
- **Memory Efficient**: Uses existing camera buffer management

## Usage

### Web API Endpoints

#### Start Recording
```bash
POST /camera/record/start
```
Response:
```json
{"status": "Recording started"}
```

#### Stop Recording
```bash
POST /camera/record/stop
```
Response:
```json
{"status": "Recording stopped"}
```

#### Check Recording Status
```bash
GET /camera/record/status
```
Response (when recording):
```json
{
  "status": "recording",
  "file": "/recordings/2024-01-15_14-30-25.mjpeg",
  "frames": 150,
  "duration": 15000
}
```

Response (when not recording):
```json
{"status": "stopped"}
```

### Programmatic Usage

```cpp
#include <ESPWiFi.h>

ESPWiFi device;

void setup() {
  device.start();
}

void loop() {
  device.runSystem(); // This automatically calls updateRecording()
  
  // Optional: Start recording programmatically
  if (someCondition) {
    device.recordCamera();
  }
  
  // Optional: Stop recording programmatically
  if (anotherCondition) {
    device.stopVideoRecording();
  }
}
```

## File Format

Recorded videos are saved as MJPEG files in the `/recordings/` directory with timestamps in the filename:
- Format: `YYYY-MM-DD_HH-MM-SS.mjpeg`
- Location: `/recordings/` directory on the filesystem
- Compatible with most video players and browsers

## Configuration

The recording functionality uses these default settings:
- **Frame Rate**: 10 FPS
- **Auto-stop Duration**: 60 seconds
- **File Format**: MJPEG
- **Storage Location**: `/recordings/` directory

You can modify these settings by editing the variables in `ESPWiFi.h`:
```cpp
int recordingFrameRate = 10; // frames per second
// Auto-stop duration is set in updateRecording() function
```

## Storage Requirements

- **Approximate Size**: ~1-2 MB per minute at 10 FPS (depends on image quality)
- **Storage**: Uses LittleFS or SD card (whichever is available)
- **Directory**: Automatically creates `/recordings/` directory if it doesn't exist

## Browser Compatibility

Recorded MJPEG files can be:
- Played directly in most modern browsers
- Opened with video players like VLC, QuickTime, etc.
- Converted to other formats using tools like FFmpeg

## Error Handling

The system includes comprehensive error handling:
- Checks for camera initialization
- Validates filesystem availability
- Ensures directory creation
- Handles file write failures
- Provides detailed logging

## Viewing Your Recordings

### Web Interface
After uploading the updated code:

1. **Open your browser** and go to: `http://[YOUR_ESP_IP]/camera/recordings`
2. **You'll see a web page** listing all your recorded videos
3. **Each video has:**
   - A clickable link to open the video in a new tab
   - An embedded video player with controls
   - File size information

### Direct File Access
You can also access recordings directly:

1. **For LittleFS**: `http://[YOUR_ESP_IP]/littlefs/recordings/[filename].mjpeg`
2. **For SD Card**: `http://[YOUR_ESP_IP]/sd/recordings/[filename].mjpeg`
3. **Example**: `http://192.168.1.100/littlefs/recordings/2024-01-15_14-30-25.mjpeg`

### File Browser
Access the general file browser at `http://[YOUR_ESP_IP]/files` and navigate to the recordings folder.

## Integration with Dashboard

The video recording functionality integrates with the existing camera dashboard module. You can extend the dashboard to include recording controls by adding buttons that call the recording API endpoints.

## Technical Details

- Uses ESP32-CAM's JPEG output directly
- Writes frames with proper MJPEG headers and boundaries
- Maintains frame rate timing using millis() intervals
- Leverages existing camera and filesystem infrastructure
- Memory-efficient streaming to filesystem
