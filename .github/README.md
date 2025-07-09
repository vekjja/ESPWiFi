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
- 🧩 Modular: add pins, WebSockets, and more via dashboard
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

```cpp
#include <ESPWiFi.h>

ESPWiFi wifi;

void setup() {
  wifi.startLog();
  wifi.startWiFi();
  wifi.startMDNS();
  wifi.startGPIO();
  wifi.srvAll();
  wifi.startWebServer();
}

void loop() {
  yield();
  wifi.streamRSSI();
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
- Edit device/network settings
- Drag-and-drop UI, Material UI theme
- Edit and sync JSON config

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
- `/config` — GET/PUT device config (JSON)
- `/gpio` — POST pin control
- `/rssi` — WebSocket for live RSSI
- `/camera/live` — Camera stream (ESP32)
- `/log` — Device logs
- `/restart` — Reboot device

---

## 📜 License

MIT © Kevin Jayne
