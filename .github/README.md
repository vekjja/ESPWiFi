# ESPWiFi

**ESPWiFi** is a modern, extensible library and dashboard for easily connecting ESP32 boards to WiFi networks, managing device settings, and controlling hardware modules via a web interface.

It supports both Station (STA) and Access Point (AP) modes, JSON-based configuration stored on LittleFS, a built-in web server, and a drag-and-drop dashboard for live device management.

---

## ✨ Features

- 📶 Easy WiFi connection (STA/AP modes)
- 📡 Auto or manual AP mode fallback
- 💾 JSON-based configuration stored on LittleFS
- 🖥️ Web dashboard for live device/module management
- 📡 mDNS support for easy device discovery
- 🔌 GPIO, PWM, and remote pin control
- 🔧 Web server API for settings, logs, and file management
- 🔁 WebSocket endpoints for live data (RSSI, camera, custom)
- 📷 Camera and spectral analyzer support (ESP32)
- 🎛️ BMI160 accelerometer/gyroscope sensor support
- 🧩 Modular: add pins, WebSockets, and more via dashboard
- 🔐 Configurable authentication (username/password)
- 🔄 Over-the-air (OTA) firmware and filesystem updates
- 🛠️ PlatformIO and Arduino IDE compatible

---

## 🛠 Installation

### PlatformIO
Add to `platformio.ini`:
```ini
lib_deps = 
  https://github.com/vekjja/ESPWiFi.git
```

### Arduino IDE
1. Download or clone this repo
2. Move the folder into your Arduino `libraries/` directory

---

## 📂 File Structure

```
ESPWiFi/
├── src/                 # Core library source files
│   ├── ESPWiFi.h        # Main library header
│   ├── Device.cpp       # Device initialization
│   ├── WiFi.cpp         # WiFi connection management
│   ├── WebServer.cpp    # HTTP server and API endpoints
│   └── ...              # Other core modules
├── include/             # Hardware libraries and headers
│   ├── WebSocket.h      # WebSocket implementation
│   ├── DFRobot_BMI160.h # BMI160 sensor library
│   ├── SpectralAnalyzer.h
│   ├── LED/             # LED matrix support
│   └── 2D/              # 2D physics engine
├── dashboard/           # React web dashboard
│   ├── src/
│   │   ├── components/  # React components
│   │   └── utils/       # Utility functions
│   ├── public/
│   │   └── config.json  # Dashboard config
│   └── package.json
├── examples/
│   └── config.json      # Example device configuration
├── data/                # Built dashboard files (for upload)
├── release/             # Pre-built firmware binaries
├── platformio.ini       # PlatformIO configuration
├── library.json         # Library metadata
└── README.md
```

---

## 🔧 Firmware Usage (C++/Arduino)

### Quick Start (Default Configuration)

The simplest way to use ESPWiFi is with the default `start()` and `runSystem()` methods:
###### ESPWiFi will generate a default `config.json` file if not present.
```cpp
#include <ESPWiFi.h>

ESPWiFi espwifi;

void setup() {
  espwifi.start();
}

void loop() {
  espwifi.runSystem();
}
```

The `start()` method automatically:
- Initializes serial communication
- Starts logging
- Reads configuration from LittleFS
- Connects to WiFi (or starts AP mode)
- Starts mDNS
- Starts the web server
- Handles configuration-based services (GPIO, camera, etc.)

The `runSystem()` method handles:
- RSSI streaming
- Camera streaming (if enabled in config)

### Manual Configuration

For more control, you can use `config.json` to configure the device and start services individually:

### Example `config.json`
```json
{
    "mode": "client",
    "mdns": "ESPWiFi",
    "client": {
        "ssid": "YOUR_SSID",
        "password": "YOUR_PASSWORD"
    },
    "ap": {
        "ssid": "ESPWiFi",
        "password": "espwifi123"
    },
    "auth": {
        "enabled": true,
        "username": "admin",
        "password": "adminpassword"
    },
    "log": {
        "enabled": true,
        "level": "info"
    },
    "rssi": {
        "displayMode": "icon"
    },
    "modules": []
}
```

### Example Custom Firmware Usage
```cpp
#include <ESPWiFi.h>

ESPWiFi espwifi;

void setup() {
  espwifi.readConfig();      // Load config from LittleFS (Can be omitted, but will be called by other services if needed) 
  espwifi.startAP();         // Explicitly Start Access Point Only
  espwifi.srvGPIO();         // Register Only GPIO endpoints 
}

void loop() {
  yield();                 // Allow other tasks to run
  // Custom main loop logic here
}
```

---

## 🖥️ Dashboard (Web UI)

The dashboard (in `dashboard/`) is a React app for live device management:
- Add/remove/reorder Pin and WebSocket modules
- Drag-and-drop UI, Material UI theme
- **File Browser**: Browse, upload, download, rename, and delete files on LittleFS and SD card with storage information and folder navigation
- Comprehensive device settings modal with tabbed interface:
  - **Info Tab**: View device information including network status, storage usage (LittleFS and SD card), memory statistics, chip details, and system uptime
  - **Network Tab**: Configure WiFi client/AP mode, SSID/password settings, and mDNS hostname
  - **Auth Tab**: Enable/disable authentication and configure username/password
  - **JSON Tab**: Direct JSON configuration editing with validation
  - **Updates Tab**: Over-the-air firmware and filesystem updates (when OTA is enabled)
- Real-time device monitoring and status updates

---

## 🧩 Hardware & Sensors
- **GPIO/PWM**: Control pins, set modes, invert, remote control
- **WebSocket**: Live RSSI, camera, or custom data
- **Camera**: ESP32-CAM/ESP32-S3 support (see `include/Camera.h`)
- **Spectral Analyzer**: Audio FFT (see `include/SpectralAnalyzer.h`)
- **BMI160**: Accelerometer/gyroscope (see `include/BMI160/`)
- **LED Matrix**: FastLED support (see `include/LEDMatrix.h`)

---

## 📚 API Endpoints

### Configuration & Device Management
- `/config` — GET/PUT device config (JSON)
- `/info` — GET device information (network, storage, memory, chip details)
- `/restart` — GET reboot device

### Authentication
- `/api/auth/login` — POST login with username/password (returns Bearer token)
- `/api/auth/logout` — POST logout and invalidate token

### GPIO Control
- `/gpio` — POST pin control

### WebSocket Streams
- `/rssi` — WebSocket for live RSSI
- `/camera` — WebSocket for camera stream (ESP32)
- `/camera/snapshot` — GET capture and download camera snapshot (ESP32)

### Over-the-Air Updates
- `/api/ota/status` — GET OTA status and device information
- `/api/ota/progress` — GET current OTA update progress
- `/api/ota/start` — POST start OTA update (firmware or filesystem)
- `/api/ota/upload` — POST upload firmware binary
- `/api/ota/filesystem` — POST upload filesystem files (supports multiple files with folder structure)

### File Management
- `/api/files` — GET list files and directories (supports `fs=sd|lfs` and `path` parameters)
- `/api/storage` — GET storage information (total, used, free) for filesystem (supports `fs=sd|lfs`)
- `/api/files/mkdir` — POST create directory
- `/api/files/rename` — POST rename file or directory
- `/api/files/delete` — POST delete file or directory
- `/api/files/upload` — POST upload file to filesystem
- `/sd/*` — GET serve files from SD card
- `/lfs/*` — GET serve files from LittleFS
- `/log` — GET device logs

---

## 📜 License

MIT © Kevin Jayne
