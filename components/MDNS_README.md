# mDNS Component

This project uses the **ESP mDNS** component from the [esp-protocols](https://github.com/espressif/esp-protocols) repository.

## Setup

The mDNS component is included as a git submodule at `components/esp-protocols`.

### Initial Clone

When cloning this repository, initialize the submodule:

```bash
git clone <your-repo-url>
cd ESPWiFi
git submodule update --init --recursive
```

### Updating mDNS

To update the mDNS component to the latest upstream version:

**Option 1: Use the update script (Recommended)**
```bash
./scripts/update_mdns.sh
```

**Option 2: Manual update**
```bash
# Update submodule to latest
git submodule update --remote components/esp-protocols

# Commit the update
git add components/esp-protocols
git commit -m "Update mDNS component to latest"
```

**Option 3: Update to specific version/tag**
```bash
cd components/esp-protocols
git fetch --tags
git checkout <tag-name>  # e.g., v1.4.0
cd ../..
git add components/esp-protocols
git commit -m "Update mDNS to <tag-name>"
```

## Configuration

mDNS can be enabled/disabled in the device configuration:

```json
{
  "wifi": {
    "mdns": true
  }
}
```

## Features

The mDNS implementation advertises:
- **HTTP service** (`_http._tcp`) on port 80 - Web dashboard
- **WebSocket service** (`_ws._tcp`) on port 80 - RSSI streaming
- **Arduino OTA service** (`_arduino._tcp`) on port 3232 - IDE discovery

## Usage

Once running, the device is accessible at:
- `http://<hostname>.local` (e.g., `http://espwifi-a1b2c3.local`)
- Shows up in Arduino IDE network ports for OTA uploads
- Discoverable in Bonjour/Avahi browsers

## Troubleshooting

**Build fails with "mdns component not found":**
- Ensure submodule is initialized: `git submodule update --init --recursive`
- Check `CMakeLists.txt` includes `components/esp-protocols/components` in `EXTRA_COMPONENT_DIRS`

**mDNS not working at runtime:**
- Check `config["wifi"]["mdns"]` is `true`
- Ensure WiFi is connected (AP or Client mode)
- Check firewall allows mDNS (UDP port 5353)
- On Windows, install Bonjour Print Services

## Documentation

- [ESP mDNS Component Docs](https://github.com/espressif/esp-protocols/tree/master/components/mdns)
- [mDNS RFC 6762](https://datatracker.ietf.org/doc/html/rfc6762)
- [DNS-SD RFC 6763](https://datatracker.ietf.org/doc/html/rfc6763)

