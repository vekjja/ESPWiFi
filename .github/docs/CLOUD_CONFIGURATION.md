# Cloud Configuration - Device-Driven Approach

## Overview

The ESPWiFi cloud pairing system uses a **device-driven configuration approach**. The device controls its own cloud settings, and the UI reads them from the device rather than hardcoding or using environment variables.

## How It Works

### 1. Device Defines Cloud Configuration

**Location:** `src/DefaultConfig.cpp`

```cpp
// Cloud Client - Connect to cloud.espwifi.io for remote access
doc["cloud"]["enabled"] = true;
doc["cloud"]["baseUrl"] = "https://cloud.espwifi.io";
doc["cloud"]["deviceId"] = getHostname();
doc["cloud"]["tunnel"] = "ws_control";
doc["cloud"]["autoReconnect"] = true;
doc["cloud"]["reconnectDelay"] = 5000;
```

### 2. Device Shares Configuration via BLE

**Location:** `src/BLE.cpp` - `get_identity` command

When the UI pairs a device via BLE, it requests the device's identity:

```javascript
// Dashboard sends:
{ cmd: "get_identity" }

// Device responds with:
{
  "ok": true,
  "cmd": "get_identity",
  "deviceName": "ESPWiFi",
  "hostname": "espwifi-ABCD12",
  "bleAddress": "AA:BB:CC:DD:EE:FF",
  "fw": "1.0.0",
  "cloud": {
    "enabled": true,
    "baseUrl": "https://cloud.espwifi.io",
    "tunnel": "ws_control"
  }
}
```

### 3. Dashboard Uses Device's Cloud Config

**BLE Pairing Flow** (`dashboard/src/components/BlePairingFlow.js`):

```javascript
// Read device identity via BLE
const identity = await writeJsonAndRead(control, { cmd: "get_identity" });

// Use cloud config from device
const cloudConfig = identity?.cloud?.enabled
  ? {
      enabled: true,
      baseUrl: identity.cloud.baseUrl,
      tunnel: identity.cloud.tunnel,
    }
  : { enabled: false };

// Save to device registry
const record = {
  id,
  name,
  hostname,
  deviceId,
  authToken,
  cloudTunnel: cloudConfig, // ← From device
  lastSeenAtMs: Date.now(),
};
```

**Claim Code Entry** (`dashboard/src/components/ClaimCodeEntry.js`):

```javascript
// Get cloud base URL from an existing device, or use default
const cloudBaseUrl = existingDevices.find(
  (d) => d?.cloudTunnel?.baseUrl
)?.cloudTunnel?.baseUrl || "https://cloud.espwifi.io";

// Use it to redeem claim code
const result = await redeemClaimCode(code, cloudBaseUrl);
```

## Benefits

### ✅ Single Source of Truth
- Device config (`DefaultConfig.cpp`) is the only place to define cloud settings
- No duplication between device and UI
- No sync issues

### ✅ Device Autonomy
- Device decides its own cloud broker
- Works without UI configuration
- Can update cloud URL via device config

### ✅ Flexibility
- Different devices can use different cloud brokers
- Enterprise deployments can use private cloud brokers
- No hardcoded URLs in dashboard

### ✅ Security
- Device generates its own credentials
- UI can't misconfigure cloud settings
- Consistent behavior across all devices

## Configuration Hierarchy

### For BLE Pairing:
```
Device DefaultConfig.cpp
  ↓
Device sends via BLE get_identity
  ↓
Dashboard reads from identity
  ↓
Dashboard stores in device registry
  ↓
Dashboard uses for cloud connection
```

### For Claim Code Entry:
```
Existing device in registry
  ↓
Dashboard reads cloudTunnel.baseUrl
  ↓
Dashboard uses for claim API call
  ↓
Falls back to default if no devices
```

## Implementation Details

### Device (ESP32)

**File:** `src/BLE.cpp`

```cpp
if (strcmp(cmd, "get_identity") == 0) {
  // ... existing identity fields ...
  
  // Include cloud configuration
  if (espwifi->config["cloud"]["enabled"] | false) {
    resp["cloud"]["enabled"] = true;
    resp["cloud"]["baseUrl"] = 
        espwifi->config["cloud"]["baseUrl"].as<const char *>();
    resp["cloud"]["tunnel"] = 
        espwifi->config["cloud"]["tunnel"].as<const char *>();
  } else {
    resp["cloud"]["enabled"] = false;
  }
}
```

### Dashboard (React)

**File:** `src/utils/apiUtils.js`

```javascript
export const redeemClaimCode = async (
  claimCode,
  cloudBaseUrl = "https://cloud.espwifi.io", // ← Default fallback
  tunnel = "ws_control"
) => {
  const url = `${cloudBaseUrl}/api/claim`;
  // ...
};
```

**File:** `src/components/ClaimCodeEntry.js`

```javascript
// Get cloud base URL from existing devices
const cloudBaseUrl = existingDevices.find(
  (d) => d?.cloudTunnel?.baseUrl
)?.cloudTunnel?.baseUrl || "https://cloud.espwifi.io";

// Use it for claim redemption
const result = await redeemClaimCode(code, cloudBaseUrl);
```

## Migration from Environment Variables

### Before (Environment Variable Approach)
```javascript
// ❌ Dashboard hardcoded or used env var
const cloudBaseUrl = process.env.REACT_APP_CLOUD_BASE_URL || 
  "https://cloud.espwifi.io";
```

**Problems:**
- Required rebuilding dashboard for different cloud brokers
- Device and dashboard could be out of sync
- No flexibility for mixed deployments

### After (Device-Driven Approach)
```javascript
// ✅ Dashboard reads from device config
const cloudConfig = deviceIdentity?.cloud?.enabled
  ? {
      enabled: true,
      baseUrl: deviceIdentity.cloud.baseUrl,
      tunnel: deviceIdentity.cloud.tunnel,
    }
  : { enabled: false };
```

**Benefits:**
- No rebuild needed
- Always in sync with device
- Supports mixed deployments

## Use Cases

### Standard Deployment
```cpp
// Device DefaultConfig.cpp
doc["cloud"]["baseUrl"] = "https://cloud.espwifi.io";
```
All devices use the standard cloud broker.

### Private Cloud Deployment
```cpp
// Device DefaultConfig.cpp
doc["cloud"]["baseUrl"] = "https://cloud.mycompany.com";
```
All devices use company's private cloud broker.

### Disabled Cloud
```cpp
// Device DefaultConfig.cpp
doc["cloud"]["enabled"] = false;
```
Device operates in local-only mode.

### Mixed Deployment
- Device A: `https://cloud.espwifi.io`
- Device B: `https://cloud.mycompany.com`
- Device C: Cloud disabled

Dashboard handles all three automatically!

## Testing

### Test Device Config Propagation
```bash
# 1. Flash device with custom cloud URL
# Edit src/DefaultConfig.cpp:
doc["cloud"]["baseUrl"] = "https://test.example.com";

# 2. Pair via BLE
# Visit http://localhost:3000
# Click "Scan for ESPWiFi Devices"
# Complete pairing

# 3. Check device registry
# Open browser DevTools → Application → Local Storage
# Look for device record:
{
  "cloudTunnel": {
    "enabled": true,
    "baseUrl": "https://test.example.com" // ← From device!
  }
}
```

### Test Claim Code with Custom Broker
```bash
# 1. First device sets cloud broker
# Pair Device A (cloud.espwifi.io)

# 2. Enter claim code for Device B
# Dashboard uses Device A's cloud broker
# Works if both devices use same broker

# 3. If no devices yet
# Dashboard falls back to default
```

## Troubleshooting

### Dashboard not using device's cloud config
1. Check device BLE response includes `cloud` field
2. Verify `get_identity` command returns cloud config
3. Check browser console for device identity log
4. Inspect device registry in localStorage

### Claim code failing with different brokers
1. Ensure all devices use same cloud broker
2. Or enter claim code when no devices exist (uses default)
3. Check which cloud URL is being used in API call log

### Device cloud config not updating
1. Cloud config is read from DefaultConfig on boot
2. Changes require device restart
3. Or use `set_config` command to update runtime config

## Summary

✅ **Device-driven** - Device defines its cloud broker
✅ **Transparent** - Dashboard automatically uses device config  
✅ **Flexible** - Supports multiple cloud brokers
✅ **No hardcoding** - No URLs in dashboard code
✅ **Single source** - DefaultConfig.cpp is the truth

The cloud configuration flows naturally from device to dashboard, maintaining consistency and enabling flexible deployments without code changes.
