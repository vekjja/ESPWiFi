# Dashboard Configuration Guide

## Environment Variables

The dashboard uses environment variables for configuration. Create a `.env.local` file in the `dashboard/` directory for local overrides.

### Cloud Configuration

#### `REACT_APP_CLOUD_BASE_URL`
**Default:** `https://cloud.espwifi.io`

Base URL for the cloud broker. Used for:
- Claim code redemption API (`/api/claim`)
- Device registry cloud tunnel configuration
- WebSocket tunnel connections

**Example:**
```bash
# Use production cloud
REACT_APP_CLOUD_BASE_URL=https://cloud.espwifi.io

# Use local cloud broker for development
REACT_APP_CLOUD_BASE_URL=http://localhost:8080
```

**Where it's used:**
- `src/utils/apiUtils.js` - `redeemClaimCode()` function
- `src/components/BlePairingFlow.js` - Device registry creation
- `src/components/ClaimCodeEntry.js` - Device registry creation

### Development & Testing

#### `REACT_APP_TEST_HOSTED_MODE`
**Default:** `false` (auto-detected from hostname)

Force hosted mode detection for development testing.

**Example:**
```bash
# Test hosted mode features on localhost
REACT_APP_TEST_HOSTED_MODE=true
```

This enables:
- Device picker and registry
- BLE pairing flow on initial load
- Cloud tunnel connection logic

**When to use:**
- Testing BLE pairing flow locally
- Testing claim code entry locally
- Testing device registry features
- Developing hosted mode features

#### `REACT_APP_API_HOST`
Override the API host for local device connections.

**Example:**
```bash
# Connect to device at espwifi.local
REACT_APP_API_HOST=espwifi.local

# Connect to device at specific IP
REACT_APP_API_HOST=192.168.1.100
```

#### `REACT_APP_API_PORT`
**Default:** `80`

Override the API port.

#### `REACT_APP_API_PROTOCOL`
**Default:** `http` (auto-upgraded to `https` on espwifi.io)

Override the API protocol.

#### `REACT_APP_WS_PROTOCOL`
**Default:** `ws` (auto-upgraded to `wss` on https pages)

Override the WebSocket protocol.

## Configuration Files

### `.env`
Base environment variables (committed to git).

### `.env.local`
Local overrides (NOT committed to git, in `.gitignore`).

**Recommended:** Use `.env.local` for development overrides.

### `.env.production`
Production-specific variables (committed to git).

## Common Configurations

### Local Development (Device on LAN)
```bash
# .env.local
REACT_APP_API_HOST=espwifi.local
REACT_APP_API_PORT=80
```

### Local Development (Test Hosted Mode)
```bash
# .env.local
REACT_APP_TEST_HOSTED_MODE=true
REACT_APP_CLOUD_BASE_URL=http://localhost:8080
```

### Production (espwifi.io)
```bash
# .env.production
REACT_APP_CLOUD_BASE_URL=https://cloud.espwifi.io
```

## How Configuration Works

### Hosted Mode Detection

**Automatic detection:**
```javascript
// src/utils/apiUtils.js
export const isHostedFromEspWiFiIo = () => {
  const hostname = window.location.hostname.toLowerCase();
  return hostname === "espwifi.io" || hostname.endsWith(".espwifi.io");
};
```

**Override for testing:**
```javascript
if (
  process.env.NODE_ENV === "development" &&
  process.env.REACT_APP_TEST_HOSTED_MODE === "true"
) {
  return true;
}
```

### Cloud Base URL

**Used in:**

1. **API Calls** (`src/utils/apiUtils.js`):
```javascript
export const redeemClaimCode = async (claimCode, tunnel = "ws_control") => {
  const cloudBaseUrl = process.env.REACT_APP_CLOUD_BASE_URL || 
    "https://cloud.espwifi.io";
  const url = `${cloudBaseUrl}/api/claim`;
  // ...
};
```

2. **Device Registry** (`src/components/BlePairingFlow.js`):
```javascript
cloudTunnel: {
  enabled: true,
  baseUrl: process.env.REACT_APP_CLOUD_BASE_URL || 
    "https://cloud.espwifi.io",
}
```

3. **Claim Code Entry** (`src/components/ClaimCodeEntry.js`):
```javascript
cloudTunnel: {
  enabled: true,
  baseUrl: process.env.REACT_APP_CLOUD_BASE_URL || 
    "https://cloud.espwifi.io",
  wsUrl: result.ui_ws_url,
}
```

### WebSocket URL Resolution

The dashboard automatically chooses between local and cloud WebSocket:

```javascript
// src/App.js
const controlWsUrl = useMemo(() => {
  if (isHostedMode && selectedDeviceId) {
    const selectedDevice = devices.find((d) => d.id === selectedDeviceId);
    if (selectedDevice?.cloudTunnel?.enabled) {
      // Use cloud tunnel from device registry
      return resolveWebSocketUrl("control", deviceConfig, {
        preferTunnel: true,
      });
    }
  }
  // Fallback to local connection
  return buildWebSocketUrl("/ws/control");
}, [isHostedMode, selectedDeviceId, devices]);
```

## Best Practices

### Don't Commit Secrets
- Never commit `.env.local` to git
- Never commit API keys or tokens to `.env`
- Use environment-specific files appropriately

### Use Defaults Wisely
- Provide sensible defaults for production
- Make local development easy with `.env.local`
- Document all environment variables

### Test Both Modes
- Test local connection (device on LAN)
- Test cloud connection (hosted mode)
- Test with `REACT_APP_TEST_HOSTED_MODE=true`

### Build-Time vs Runtime
- React environment variables are **baked in at build time**
- Changes require rebuild (`npm run build`)
- Values cannot change at runtime

## Troubleshooting

### Environment variables not working
1. Check variable name starts with `REACT_APP_`
2. Restart dev server after changing `.env` files
3. Rebuild for production (`npm run build`)

### Cloud connection failing
1. Check `REACT_APP_CLOUD_BASE_URL` is correct
2. Verify cloud broker is running
3. Check network allows HTTPS/WSS to cloud URL
4. Look for CORS errors in browser console

### Hosted mode not activating
1. Check hostname is `espwifi.io` or `*.espwifi.io`
2. Or set `REACT_APP_TEST_HOSTED_MODE=true` for testing
3. Clear localStorage and reload if stale state

### Mixed content errors
- Chrome/Safari block `ws://` on `https://` pages
- Dashboard automatically upgrades to `wss://`
- Check browser console for security warnings

## Reference

**React Docs:** [Environment Variables](https://create-react-app.dev/docs/adding-custom-environment-variables/)

**ESPWiFi Cloud Docs:** See `/docs/CLOUD_PAIRING_README.md`
