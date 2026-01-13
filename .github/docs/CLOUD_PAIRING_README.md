# Cloud Pairing Documentation Index

This directory contains complete documentation for the ESPWiFi cloud pairing system.

## Quick Start

**For Users**: Pair a device in 3 simple steps:
1. Visit https://espwifi.io
2. Click "Scan for ESPWiFi Devices" or "Enter claim code"
3. Follow the prompts → done!

**For Developers**: Everything works out of the box!
- Device: `cloud.enabled = true` by default in `src/DefaultConfig.cpp`
- Dashboard: Automatically uses cloud tunnel when available
- Broker: Running at https://cloud.espwifi.io

## Documentation Files

### 📘 [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)
**Start here!** Complete summary of what was implemented and how it works.

**Contents:**
- What was implemented
- Key design decisions
- File changes summary
- Complete flow diagrams
- Testing instructions
- Troubleshooting guide

### 📗 [CLOUD_PAIRING_FLOW.md](CLOUD_PAIRING_FLOW.md)
**Deep dive** into the complete pairing flow with technical details.

**Contents:**
- Architecture overview
- Design philosophy
- Detailed flow for BLE pairing
- Detailed flow for claim code entry
- QR code support (implementation ready)
- Security considerations
- Testing procedures

### 📊 [ARCHITECTURE_DIAGRAM.txt](ARCHITECTURE_DIAGRAM.txt)
**Visual reference** with ASCII diagrams showing the complete system.

**Contents:**
- System architecture
- BLE pairing flow (step-by-step)
- Claim code flow (step-by-step)
- Configuration points
- Security model
- Design philosophy

### 💻 [examples/qr_code_example.cpp](examples/qr_code_example.cpp)
**Code examples** for implementing QR code generation on devices.

**Contents:**
- QR code generation using ricmoo/QRCode library
- OLED/LCD display examples
- ASCII QR for serial console
- Web endpoint serving SVG QR codes

## How It Works

### The Simple Version

1. **Device boots** with cloud enabled (default setting)
2. **User pairs** via BLE (WiFi setup) or claim code (instant pairing)
3. **Device connects** to cloud broker automatically
4. **Dashboard connects** via cloud tunnel to device
5. **User controls** device from anywhere via espwifi.io

### The Key Insight

**The device controls its own configuration.** The UI doesn't configure cloud settings - the device reads them from `DefaultConfig.cpp`. This makes devices autonomous, simplifies BLE pairing, and ensures consistent behavior.

## Pairing Methods

### Method 1: BLE Pairing
**When to use:** Initial device setup, local pairing

**Steps:**
1. Visit espwifi.io
2. Click "Scan for ESPWiFi Devices"
3. Select device from Bluetooth picker
4. Enter WiFi SSID and password
5. Done! Device auto-connects to cloud

**Technical flow:**
```
User → Dashboard → BLE → Device → WiFi restart → 
Cloud connection (from DefaultConfig) → Dashboard connects via tunnel
```

### Method 2: Claim Code Entry
**When to use:** Remote pairing, already-configured devices

**Steps:**
1. Get claim code from device (logs/display/QR)
2. Visit espwifi.io → "Enter claim code"
3. Type code (e.g., "ABC123")
4. Done! Instant cloud access

**Technical flow:**
```
Device generates claim → User enters code → 
Cloud broker validates → Dashboard gets credentials → 
Dashboard connects via tunnel
```

## Configuration

### Device (ESP32)

**Location:** `src/DefaultConfig.cpp` lines 39-45

```cpp
// Cloud Client - Connect to cloud.espwifi.io for remote access
doc["cloud"]["enabled"] = true;  // ← On by default
doc["cloud"]["baseUrl"] = "https://cloud.espwifi.io";
doc["cloud"]["deviceId"] = getHostname();
doc["cloud"]["tunnel"] = "ws_control";
doc["cloud"]["autoReconnect"] = true;
doc["cloud"]["reconnectDelay"] = 5000;
```

**To disable cloud** (advanced users):
```cpp
doc["cloud"]["enabled"] = false;  // Device won't connect to cloud
```

### Dashboard (React)

**Hosted Mode Detection:** `src/utils/apiUtils.js`

```javascript
export const isHostedFromEspWiFiIo = () => {
  return hostname === "espwifi.io" || hostname.endsWith(".espwifi.io");
};
```

**Cloud Base URL Configuration:** Environment variable

```bash
# .env or .env.local
REACT_APP_CLOUD_BASE_URL=https://cloud.espwifi.io
```

The dashboard uses `REACT_APP_CLOUD_BASE_URL` for:
- Claim code redemption API calls
- Device registry cloud tunnel base URL
- Defaults to `https://cloud.espwifi.io` if not set

**Cloud Connection:** `src/App.js`

The dashboard automatically uses cloud WebSocket when:
- Running in hosted mode (espwifi.io)
- Device has `cloudTunnel.enabled = true` in registry
- Auth token is available

### Cloud Broker (Go)

**Environment Variables:**
```bash
LISTEN_ADDR=:8080
PUBLIC_BASE_URL=https://cloud.espwifi.io
LOG_LEVEL=info
```

**Optional global auth:**
```bash
DEVICE_AUTH_TOKEN=secret1  # Require for device connections
UI_AUTH_TOKEN=secret2      # Require for UI connections
```

## API Reference

### Device → Cloud Broker

**WebSocket Connection:**
```
wss://cloud.espwifi.io/ws/device/{deviceId}?tunnel={tunnel}&claim={claim}&announce=1
```

**Parameters:**
- `deviceId`: Device hostname/MAC
- `tunnel`: Tunnel identifier (e.g., "ws_control")
- `claim`: 6-character claim code for pairing
- `announce=1`: Request registration message

**Registration Response:**
```json
{
  "type": "registered",
  "device_id": "espwifi-ABCD12",
  "tunnel": "ws_control",
  "ui_ws_url": "wss://cloud.espwifi.io/ws/ui/espwifi-ABCD12?tunnel=ws_control",
  "device_ws_url": "wss://cloud.espwifi.io/ws/device/espwifi-ABCD12?tunnel=ws_control",
  "ui_token_required": true
}
```

### Dashboard → Cloud Broker

**Claim Code Redemption:**
```http
POST /api/claim
Content-Type: application/json

{
  "code": "ABC123",
  "tunnel": "ws_control"
}
```

**Response:**
```json
{
  "ok": true,
  "code": "ABC123",
  "device_id": "espwifi-ABCD12",
  "tunnel": "ws_control",
  "ui_ws_url": "wss://cloud.espwifi.io/ws/ui/espwifi-ABCD12?tunnel=ws_control",
  "token": "abc123...",
  "ui_ws_token": "wss://cloud.espwifi.io/ws/ui/espwifi-ABCD12?tunnel=ws_control&token=abc123..."
}
```

**WebSocket Connection:**
```
wss://cloud.espwifi.io/ws/ui/{deviceId}?tunnel={tunnel}&token={token}
```

## Security

### Claim Codes
- ✅ 10-minute expiration
- ✅ One-time use (consumed immediately)
- ✅ Random generation (6 alphanumeric, avoiding O/0, I/1)
- ⚠️ Not secrets (meant to be shared temporarily)

### Auth Tokens
- ✅ Generated on device
- ✅ Required for WebSocket connections
- ✅ Validated by broker using constant-time comparison
- ⚠️ ARE secrets (never log, display, or share)

### Best Practices
1. Always use WSS (not WS) in production
2. Tokens passed via URL query param (WebSocket limitation)
3. CORS configured on cloud broker
4. Device generates new claim code on restart

## Troubleshooting

### Device won't connect to cloud
1. ✓ Check `cloud.enabled = true` in DefaultConfig.cpp
2. ✓ Verify WiFi client mode working (not AP only)
3. ✓ Check network allows outbound WSS (port 443)
4. ✓ Look for errors in device logs:
   ```
   ☁️ Starting cloud client
   ☁️ Claim code: ABC123
   ☁️ Device registered with cloud
   ```

### Dashboard can't connect via tunnel
1. ✓ Device shows "Device registered with cloud" in logs
2. ✓ Claim code hasn't expired (10 minutes)
3. ✓ Correct auth token in device registry
4. ✓ Check browser console for WebSocket errors
5. ✓ Verify token matches between device and dashboard

### Claim code doesn't work
1. ✓ Check expiration (10 minutes from device registration)
2. ✓ Code only works once (consumed on redemption)
3. ✓ Device must be connected to cloud first
4. ✓ Tunnel parameter must match ("ws_control")
5. ✓ Check broker logs for claim validation errors

## Testing

### Quick Test (BLE Pairing)
```bash
# 1. Flash device
pio run -t upload -t monitor

# 2. Start dashboard
cd dashboard
npm start

# 3. Open browser
# - Visit http://localhost:3000
# - Set REACT_APP_TEST_HOSTED_MODE=true for testing
# - Click "Scan for ESPWiFi Devices"
# - Follow pairing flow
```

### Quick Test (Claim Code)
```bash
# 1. Get claim code from device logs
# Look for: "☁️ Claim code: ABC123"

# 2. Visit dashboard
# - Open https://espwifi.io
# - Click device picker → "Enter claim code"
# - Enter code → "Claim Device"

# 3. Verify connection
# - Dashboard should show device online
# - WebSocket URL should show cloud.espwifi.io
```

## Future Enhancements

### QR Code Scanning (Dashboard)
**Status:** Infrastructure ready, needs library integration

**To implement:**
```bash
cd dashboard
npm install html5-qrcode
```

Update `src/components/ClaimCodeEntry.js` (placeholder commented in code).

### QR Code Display (Device)
**Status:** Example code provided

**To implement:**
```ini
# platformio.ini
lib_deps = 
  ricmoo/QRCode @ ^0.0.1
  adafruit/Adafruit GFX Library
  adafruit/Adafruit SSD1306
```

See `examples/qr_code_example.cpp` for implementation.

### Deep Linking
**Status:** Not implemented

**To implement:**
- Add route handler for `/claim?code=ABC123` in dashboard
- Auto-open ClaimCodeEntry with pre-filled code
- Enables QR codes to directly open pairing flow

### Multi-User Support
**Status:** Not implemented

**Future features:**
- User accounts
- Device sharing
- Permission levels (read-only vs full control)
- Device discovery by account

## Contributing

When making changes to the cloud pairing system:

1. **Update documentation** if you change behavior
2. **Test both pairing methods** (BLE and claim code)
3. **Check security implications** of any auth changes
4. **Maintain device autonomy** (no UI-driven config)
5. **Keep it simple** for end users

## Support

- **Issues:** https://github.com/[your-repo]/issues
- **Documentation:** This directory
- **Examples:** `examples/` directory

## License

[Your license here]

---

**Ready to use!** The complete cloud pairing system is implemented and production-ready. 🚀
