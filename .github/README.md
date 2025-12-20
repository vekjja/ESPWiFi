# ESPWiFi

**ESPWiFi** is a modern, extensible library and dashboard for easily connecting ESP8266 and ESP32 boards to WiFi networks, managing device settings, and controlling hardware modules via a web interface. It supports both Station (STA) and Access Point (AP) modes, JSON-based configuration with LittleFS, a built-in web server, and a drag-and-drop dashboard for live device management.

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
- 📊 Device information dashboard (network, storage, memory, chip details)
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
├── src/
│   └── ESPWiFi.h / .cpp
├── dashboard/           # React web dashboard
├── include/             # Hardware, sensors, camera, etc.
├── examples/
│   └── config.json      # Example config
├── library.json
└── README.md
```

---

## 🔧 Firmware Usage (C++/Arduino)

### Quick Start (Default Configuration)

The simplest way to use ESPWiFi is with the default `start()` and `runSystem()` methods:

```cpp
#include <ESPWiFi.h>

ESPWiFi espwifi;

void setup() {
  espwifi.start();  // Initializes serial, logging, config, WiFi, mDNS, and web server
}

void loop() {
  espwifi.runSystem();  // Handles RSSI streaming and camera (if enabled)
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

### Manual Service Configuration

For more control, you can start services individually:

```cpp
#include <ESPWiFi.h>

ESPWiFi espwifi;

void setup() {
  espwifi.startSerial();      // Initialize serial (optional, defaults to 115200)
  espwifi.startLogging();     // Start file logging
  espwifi.readConfig();        // Load config from LittleFS
  espwifi.startWiFi();         // Connect to WiFi or start AP
  espwifi.startMDNS();         // Start mDNS service
  espwifi.srvAll();            // Register all API endpoints (GPIO, files, config, etc.)
  espwifi.startWebServer();    // Start web server
  
  // Optional services (if enabled in config or needed)
  // espwifi.startOTA();         // Enable OTA updates
  // espwifi.startCamera();      // Start camera (ESP32-CAM/ESP32-S3)
  // espwifi.startBMI160();      // Initialize BMI160 sensor
  // espwifi.startLEDMatrix();   // Start LED matrix
  // espwifi.startSpectralAnalyzer(); // Start spectral analyzer
}

void loop() {
  yield();                    // Allow other tasks to run
  espwifi.streamRSSI();       // Stream RSSI data via WebSocket
  
  // Optional streaming (if enabled)
  // espwifi.streamCamera();    // Stream camera frames (if enabled)
}
```

### Example `config.json`
```json
{
  "mode": "client", // or "ap"
  "mdns": "esp32",
  "client": {
    "ssid": "YourWiFi",
    "password": "YourPassword"
  },
  "ap": {
    "ssid": "ESPWiFi-AP",
    "password": "abcd1234"
  },
  "auth": {
    "enabled": false,
    "username": "admin",
    "password": "password"
  },
  "modules": [
    {
      "type": "pin",
      "number": 5,
      "mode": "out",
      "state": "low",
      "name": "LED"
    },
    {
      "type": "webSocket",
      "url": "/rssi",
      "payload": "text",
      "name": "RSSI"
    }
  ]
}
```

> **Note:** When building the dashboard, place your desired `config.json` in `./dashboard/public` so it is included in the build output. Alternatively, you can manually add `config.json` to the root of the `data` directory before uploading to the ESP device.

Upload config with:
```bash
pio run --target uploadfs
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

### Quick Start
```bash
cd dashboard
npm install
npm start
# Open http://localhost:3000
```

### Build for ESP device
```bash
npm run build:uploadfs
```

> **Tip:** When developing, you can create a `.env` file in the root of the `dashboard/` directory to set the API endpoint for your ESP device. For example:
> ```env
> REACT_APP_API_HOST=espwifi.local
> REACT_APP_API_PORT=80
> ```
> This allows the dashboard to connect to your device on the local network during development.

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
