# ESPWiFi Dashboard

A modern, drag-and-drop web dashboard for managing ESPWiFi devices, modules, and settings. Built with React, Material UI, and dnd-kit, this dashboard provides a user-friendly interface for configuring ESP32/ESP8266 devices running the ESPWiFi firmware.

---

## 🚀 Features

- **Live Device Configuration**: View and edit device settings (WiFi, mDNS, AP/client mode) via a modal interface.
- **Module Management**: Add, remove, and reorder Pin and WebSocket modules with drag-and-drop.
- **Pin Control**: Toggle GPIO pins, set PWM, invert logic, and send commands to local or remote ESP devices.
- **WebSocket Integration**: Add WebSocket modules for live data (e.g., RSSI, camera, custom streams), send/receive messages, and configure payload type.
- **JSON Config Editor**: Edit the full device configuration as JSON, with validation and device sync.
- **Responsive UI**: Works on desktop and mobile, with a dark theme and Material UI components.
- **PlatformIO Integration**: Build, upload, and monitor firmware directly from the dashboard project root.

---

## 📦 Installation & Setup

1. **Install dependencies:**
   ```bash
   cd dashboard
   npm install
   ```

2. **Start the dashboard (development):**
   ```bash
   npm start
   # Open http://localhost:3000
   ```

3. **Build for production:**
   ```bash
   npm run build
   # Output goes to ../data for ESPWiFi static hosting
   ```

---

## 🐳 Docker (production)

This repo includes a production `dashboard/Dockerfile` that builds a static bundle with `react-scripts` and serves it with nginx.

- **Create `dashboard/.env.prod`** (not committed) to force the API host when the UI is served from `espwifi.io`:

```bash
REACT_APP_API_HOST=espwifi.local
REACT_APP_API_PORT=80
REACT_APP_API_PROTOCOL=http:
REACT_APP_WS_PROTOCOL=ws:
```

- **Build + run:**

```bash
cd dashboard
docker build -t espwifi-dashboard:prod --build-arg ENV_FILE=.env.prod .
docker run --rm -p 3000:3000 espwifi-dashboard:prod
```

For development containers, use `dashboard/Dockerfile.dev`.

4. **Upload static files to ESP device (via PlatformIO):**
   ```bash
   npm run build:uploadfs
   # Or use the combined scripts in package.json for build/upload/monitor
   ```

---

## 🖥️ Usage Overview

### Main App Structure

```jsx
import React from 'react';
import App from './App';

// Entry point
<App />
```

The dashboard fetches device configuration from `/config` and displays the mDNS name, settings modal, and all modules. All changes are local until saved to the device.

### Module Management

- **Add Module**: Click the `+` floating button to add a Pin or WebSocket module.
- **Drag & Drop**: Reorder modules by dragging their cards.
- **Edit/Delete**: Use the settings or delete icons on each module card.

#### Example: Adding a Pin Module

```jsx
<AddModule config={config} saveConfig={setConfig} />
```

#### Example: Pin Module Usage

```jsx
<PinModule
  pinNum={5}
  initialProps={{ name: 'LED', mode: 'out', state: 'low' }}
  config={config}
  onUpdate={updateModule}
  onDelete={deleteModule}
/>
```

#### Example: WebSocket Module Usage

```jsx
<WebSocketModule
  index={0}
  initialProps={{ name: 'RSSI', url: '/rssi', payload: 'text' }}
  onUpdate={updateModule}
  onDelete={deleteModule}
/>
```

### Device Settings & Configuration

- **Settings Modal**: Click the gear icon to open device/network settings.
- **Config Modal**: Click the floppy disk icon to edit the full JSON config.
- **Restart**: Use the restart button in the settings modal to reboot the device.

#### Example: Open Settings Modal

```jsx
<NetworkSettingsModal
  config={config}
  saveConfig={setConfig}
  saveConfigToDevice={saveConfigToDevice}
/>
```

---

## ⚙️ Scripts

- `npm start` — Start development server
- `npm run build` — Build static files for ESP device
- `npm run test` — Run tests
- `npm run eject` — Eject Create React App
- `npm run build:uploadfs` — Build and upload static files to ESP device
- `npm run pio:*` — PlatformIO build/upload/monitor helpers (see `package.json`)

---

## 🛠️ Dependencies

- React 19
- Material UI 6
- dnd-kit (drag-and-drop)
- PlatformIO (for firmware upload)

---

## 📁 Project Structure

```
dashboard/
  src/
    components/   # All UI components (modules, modals, buttons)
    App.js        # Main app logic
    index.js      # Entry point
    index.css     # Global styles
  public/
  package.json
  README.md
```

---

## 📝 License

MIT © Kevin Jayne
