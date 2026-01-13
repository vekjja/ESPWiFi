# ESPWiFi Device Setup via Bluetooth

Quick guide to configure your ESPWiFi device using the nRF Connect mobile app.

## What You'll Need

- **nRF Connect app** (free)
  - [iOS App Store](https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403)
  - [Google Play Store](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp)
- Your ESPWiFi device powered on
- WiFi network name (SSID) and password

## Setup Process

### Step 1: Prepare Your Device

1. Power on your ESPWiFi device
2. Wait for the device to start (about 5-10 seconds)
3. The device will start in **Access Point mode** and enable Bluetooth automatically

### Step 2: Open nRF Connect

1. Launch the **nRF Connect** app on your phone
2. Enable Bluetooth if prompted
3. Tap **"Scanner"** at the bottom of the screen

### Step 3: Find Your Device

1. Look for a device named **"ESPWiFi"** in the scan results
2. You may see additional info like:
   - Device name: `ESPWiFi` or `espwifi-XXXXXX`
   - Signal strength (RSSI)
   - MAC address
3. Tap **"CONNECT"** next to your device

> **Note**: The app may show "Pairing..." briefly - this is automatic and secure.

### Step 4: Navigate to Services

1. Once connected, you'll see a list of services
2. Scroll down to find the service with UUID:
   ```
   6E400001-B5A3-F393-E0A9-E50E24DCCA9E
   ```
   (This is the ESPWiFi control service)
3. Expand this service by tapping the down arrow

### Step 5: Find the Control Characteristic

1. Inside the service, find the characteristic with UUID:
   ```
   6E400002-B5A3-F393-E0A9-E50E24DCCA9E
   ```
2. This is the control characteristic where you'll send commands

### Step 6: Send WiFi Credentials

1. Tap the **"Upload"** icon (↑) next to the control characteristic
2. Change format from **"UINT8"** to **"UTF-8 String"**
3. Copy and paste this template, replacing with your WiFi info:

```json
{"cmd":"set_wifi","ssid":"YourNetworkName","password":"YourWiFiPassword"}
```

**Example:**
```json
{"cmd":"set_wifi","ssid":"HomeNetwork","password":"MySecurePassword123"}
```

4. Tap **"SEND"**

### Step 7: Verify Configuration

1. Tap the **"Download"** icon (↓) on the same characteristic
2. You should see a response like:
```json
{"ok":true,"cmd":"set_wifi","wifi_restart_queued":true}
```

✅ **Success!** Your device will now:
- Save the WiFi configuration
- Restart WiFi
- Connect to your network

❌ **If you see an error:**
```json
{"ok":false,"error":"ssid_required"}
```
- Check that your SSID is not empty
- Make sure the JSON format is correct (no typos, proper quotes)

### Step 8: Find Your Device on WiFi

1. Wait 10-20 seconds for the device to connect
2. Your device should now be accessible at:
   - **http://espwifi.local** (mDNS)
   - Or check your router for the device's IP address

## Optional: Get Device Information

Before configuring WiFi, you can check device details:

1. Send this command:
```json
{"cmd":"get_identity"}
```

2. Response will show:
```json
{
  "ok":true,
  "cmd":"get_identity",
  "deviceName":"ESPWiFi",
  "hostname":"espwifi-f1c1f0",
  "bleAddress":"f2:c1:f1:4e:b5:80",
  "fw":"v1.0.0"
}
```

## Security Features

Your ESPWiFi device uses **encrypted Bluetooth connections** to protect your WiFi password:

✅ **Protected:**
- WiFi credentials are encrypted over Bluetooth
- Protection against casual eavesdropping
- Automatic pairing (no PIN needed)
- Secure key storage

⚠️ **Best Practices:**
- Configure your device in a trusted location (home/office)
- Avoid public spaces when entering WiFi credentials
- Use strong WiFi passwords

## Troubleshooting

### Can't Find Device
- Make sure Bluetooth is enabled on your phone
- Ensure the device is powered on and nearby (within 10m/30ft)
- Try stopping and restarting the scan in nRF Connect
- Wait a few seconds after powering on the device

### Connection Failed
- Move closer to the device
- Make sure no other app is connected to the device
- Restart the device and try again

### "ssid_required" Error
- Check that SSID field is not empty
- Verify JSON format is correct (use the exact template above)
- Make sure you didn't accidentally include extra spaces

### Device Not Connecting to WiFi
- Double-check your WiFi password (it's case-sensitive!)
- Ensure your network name (SSID) is correct
- Verify the device is within range of your WiFi router
- Check that your WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)

### Device Disconnects Immediately
- This is normal! After sending WiFi credentials, the device may disconnect
- Wait 10-20 seconds for it to connect to your WiFi network
- Then access it via the web interface at http://espwifi.local

## Alternative: Web-Based Setup

If you prefer a graphical interface:

1. Visit **https://espwifi.io** in Chrome or Edge (desktop or mobile)
2. Click **"Scan for ESPWiFi Devices"**
3. Select your device from the browser's Bluetooth picker
4. Follow the on-screen prompts to enter WiFi credentials

> **Note**: Web Bluetooth requires Chrome, Edge, or Opera browser.

## Need Help?

- Check device logs via serial monitor (115200 baud)
- Look for messages starting with `🔵` (Bluetooth) or `📶` (WiFi)
- Visit the GitHub repository for more documentation

---

**Last Updated**: January 2026  
**Supported Platforms**: iOS, Android (via nRF Connect app)
