# ESPWiFi

**ESPWiFi** is a firmware library and web dashboard for ESP32 boards. It connects the device to WiFi, stores settings as JSON on LittleFS, and exposes a live dashboard for pins, camera, music, files, and device info.

The device speaks over two WebSockets. `/ws/control` carries settings, logs, GPIO, and file listings. `/ws/media` carries file bytes, music, and camera frames. HTTP serves the dashboard and a few small routes.

---

## ✨ Features

- 📶 Station and access point WiFi, with mDNS
- 💾 JSON config on LittleFS. WiFi connection changes are saved and applied on the next restart
- 💿 Optional SD card (SPI) for files, logs, and music
- 🖥️ React dashboard with a drag-and-drop board
- 🧩 Pin, PWM, camera, music, logs, and system info modules
- 📁 File browser for LittleFS and the SD card
- 🔐 Optional username and password authentication
- 🛠️ ESP-IDF firmware, built with PlatformIO

---

## 📟 Boards

The default PlatformIO environment is `esp32-s3`.

| Environment | Board | Notes |
| --- | --- | --- |
| `esp32-s3` | Seeed XIAO ESP32-S3 | Camera and SD card |
| `esp32-c3` | Seeed XIAO ESP32-C3 | |
| `cyd` | ESP32-2432S028R | TFT |
| `esp32u` | ESP32 | DAC build flag |

---

## 🛠 Installation

### PlatformIO

```ini
lib_deps =
  https://github.com/vekjja/ESPWiFi.git
```

---

## 📂 File structure

```
ESPWiFi/
├── src/                     # Firmware
│   ├── ESPWiFi.cpp          # start() and runSystem()
│   ├── ControlSocket.cpp    # /ws/control
│   ├── MediaSocket.cpp      # /ws/media
│   ├── Camera.cpp
│   ├── Audio.cpp
│   ├── net/                 # WiFi, HTTP server, mDNS
│   └── routes/              # Auth, root, and static files
├── include/                 # Public headers and board pin maps
├── dashboard/               # React dashboard
├── data/                    # Built dashboard, uploaded to LittleFS
├── examples/
├── partitions/
└── platformio.ini
```

---

## 🔧 Firmware

`start()` loads the default config, mounts LittleFS, reads `config.json`, mounts the SD card when the board has one, and starts logging. Boards with a TFT or Bluetooth audio start those as well.

`toggleWiFi()` brings the radio up or down. When WiFi starts it also starts mDNS, the HTTP server, `/ws/control`, and `/ws/media`.

`runSystem()` is the main loop. It feeds the watchdog, flushes logs, applies queued config updates, checks the SD card, and streams the camera when one is installed.

```cpp
#include "ESPWiFi.h"

ESPWiFi espwifi;

extern "C" void app_main(void) {
  espwifi.start();
  espwifi.toggleWiFi();
  espwifi.runSystem();
}
```

If `config.json` is missing, the firmware writes a default one. Access point mode is the default. The access point name comes from the device hostname.

### 📋 Example config

```json
{
  "deviceName": "ESPWiFi",
  "wifi": {
    "enabled": true,
    "mode": "client",
    "mdns": true,
    "client": {
      "ssid": "YOUR_SSID",
      "password": "YOUR_PASSWORD"
    },
    "accessPoint": {
      "ssid": "ESPWiFi",
      "password": "YOUR_AP_PASSWORD"
    },
    "power": {
      "txPower": 19.5,
      "powerSave": "none"
    }
  },
  "auth": {
    "enabled": false,
    "username": "admin",
    "password": "YOUR_PASSWORD"
  },
  "log": {
    "enabled": true,
    "level": "info",
    "useSD": true
  }
}
```

`wifi.mode` is `client` or `accessPoint`. Saving a new SSID or password stores it. The radio keeps the current session until the device restarts.

---

## 🖥️ Dashboard

The app in `dashboard/` is a React dashboard. From that directory:

| Command | What it does |
| --- | --- |
| `pnpm start` | Dev server |
| `pnpm run build` | Production build into `../data` |
| `pnpm upload` | Flash firmware, then open the serial monitor |
| `pnpm uploadfs` | Flash the LittleFS image, then open the serial monitor |
| `pnpm run update` | Flash the filesystem, then the firmware, then open the monitor |
| `pnpm run deploy` | Clean, rebuild the dashboard, then run `update` |

`pnpm update` and `pnpm deploy` are pnpm commands, so the scripts above need `pnpm run`.

The board is stored in the browser. Card layout and module settings stay in `localStorage` and are not sent to the device. Camera sensor settings are the exception: those are saved on the device.

Modules you can add:

- 🔌 **Pin** for GPIO and PWM
- 📡 **WebSocket** for a custom stream
- 📷 **Camera** on boards that have one. A new camera opens at the height of the visible dashboard
- 🎵 **Music Player**, one at a time. It lists audio on the SD card, default directory `/music`, and plays `.mp3`, `.wav`, `.ogg`, `.m4a`, `.aac`, and `.flac` over `/ws/media`
- 📊 **System info** cards for memory, network, and storage, one of each
- 📜 **Logs**

The movie player source is in `dashboard/src/components/modules/movie` and is not imported by the app, so it is left out of the LittleFS image.

Device settings cover network status, WiFi client and access point, transmit power, authentication, memory, storage, chip details, and a JSON editor.

The file browser lists, creates, renames, and deletes files through `/ws/control`. Uploads and downloads go through `/ws/media`.

---

## 🔁 WebSockets

Connect to `ws://<device>/ws/control` or `ws://<device>/ws/media`. With mDNS that host is the device hostname, for example `ws://espwifi.local/ws/control`.

Send a JSON text frame `{"cmd":"..."}`. Replies include `ok` and the same `cmd`.

### 🎛️ `/ws/control`

| Command | Purpose |
| --- | --- |
| `ping` | Heartbeat |
| `get_status` | IP, hostname, and WiFi mode |
| `get_config` / `set_config` | Read or merge device config |
| `get_info` | Network, storage, memory, and chip details |
| `get_rssi` | Current station signal |
| `get_logs` | A chunk of the log file |
| `set_gpio` / `get_gpio` / `set_pwm` | Pin control |
| `list_files` | List a LittleFS (`lfs`) or SD (`sd`) directory |
| `get_storage` | Total, used, and free space |
| `mkdir` / `rename_file` / `delete_file` | Change files |
| `take_snapshot` | Capture a camera frame |

### 📱 `/ws/media`

| Command | Purpose |
| --- | --- |
| `file_up_start` | Begin an upload. Binary frames follow, then the upload completes |
| `file_up_abort` | Cancel an upload |
| `file_down_start` / `file_down_next` / `file_down_stop` | Download a file in chunks |
| `media_start` / `media_next` / `media_seek` / `media_stop` | Stream an audio file |
| `media_read` | Read a byte range from an open file |
| `camera_start` / `camera_frame` / `camera_stop` | Camera frames |

Each media command is acknowledged with a JSON frame. File and stream bytes follow as a binary frame.

---

## 🌐 HTTP

| Route | Purpose |
| --- | --- |
| `GET /` | Dashboard `index.html` from LittleFS |
| `POST /api/auth/login` | Username and password login |
| `POST /api/auth/logout` | End the session |
| `POST /api/restart` | Reboot the device |

Other paths serve static dashboard files from LittleFS. Authentication is off unless `auth.enabled` is true.

---

## 📜 License

MIT © Kevin Jayne
