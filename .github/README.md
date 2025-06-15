# ESPWiFi

**ESPWiFi** is a lightweight library for easily connecting ESP8266 and ESP32 boards to WiFi networks. It supports both Station (STA) and Access Point (AP) modes, includes configuration storage via LittleFS, and is designed for use with the Arduino and PlatformIO frameworks.

---

## ✨ Features

- 📶 Easy connection to WiFi networks
- 📡 Auto or manual AP mode fallback
- 💾 JSON-based configuration stored on LittleFS
- 🔌 Works on both ESP8266 and ESP32
- 🔧 Web server interface for settings
- 🔁 Utility functions for scheduled tasks

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
├── data/
│   └── config.json       # optional, example config
├── examples/
│   └── BasicUsage/       # coming soon
├── library.json
└── README.md
```

---

## 🔧 Usage

```cpp
#include <ESPWiFi.h>

ESPWiFi wifi;

void setup() {
  wifi.start();  // Initializes FS, reads config, connects or starts AP
}

void loop() {
  wifi.handleClient();  // Handles web requests if AP/web server is active
}
```

---

## 📁 Configuration (`config.json`)

```json
{
  "ssid": "YourWiFi",
  "password": "YourPassword",
  "mode": "sta"   // or "ap"
}
```

Place this in `/data/config.json` and upload using:

```bash
pio run --target uploadfs
```

---

## 📜 License

MIT © Kevin Jayne
