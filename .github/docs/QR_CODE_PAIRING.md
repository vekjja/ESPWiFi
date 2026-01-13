# QR Code Pairing - Use Case & Implementation

## Purpose

QR codes provide a **quick initial pairing** method for ESPWiFi devices, especially useful when:
- Bluetooth is unavailable (desktop browsers, some mobile browsers)
- Multiple users need access to the same device
- Quick setup is needed without typing claim codes

## Important: Initial Pairing Only

### ✅ QR Codes Are For:

1. **First-time pairing** - User has never connected to this device
2. **Sharing access** - Giving other users access to your device
3. **Re-pairing** - Browser localStorage was cleared

### ❌ QR Codes Are NOT For:

1. **Already-paired browsers** - Device is stored in localStorage
2. **Every connection** - Only needed once per browser
3. **Authentication** - The claim code expires after 10 minutes

## How It Works

### User Flow

```
User with unpaired browser
  ↓
Visits espwifi.io on phone/computer
  ↓
Device shows QR code on screen
  ↓
User scans QR with phone camera
  ↓
Browser opens: espwifi.io/claim?code=ABC123
  ↓
Dashboard extracts claim code
  ↓
Calls /api/claim → Gets device credentials
  ↓
Stores device in localStorage
  ↓
✅ Paired! QR code not needed again on this browser
```

### Why localStorage?

Once paired via QR (or BLE or manual claim code entry):
```javascript
// Dashboard stores device in browser localStorage
const device = {
  id: "espwifi-ABCD12",
  name: "ESPWiFi",
  authToken: "long-lived-token",
  cloudTunnel: {
    enabled: true,
    baseUrl: "https://cloud.espwifi.io"
  }
};
localStorage.setItem('devices', JSON.stringify([device]));
```

**Next time user visits espwifi.io:**
- Dashboard reads device from localStorage
- Connects directly using stored credentials
- No QR code needed!

## QR Code Contents

### Recommended: URL Format (Deep Link)

```
https://espwifi.io/claim?code=ABC123
```

**Why URL format is best:**
- Phone camera apps auto-recognize URLs
- Opens browser directly to pairing page
- Works with both iOS and Android
- No typing needed

### Alternative: Simple Format

```
ABC123
```

**Simpler QR code (fewer pixels), but:**
- User must type code manually
- No automatic browser redirect
- Higher chance of typos

### Alternative: JSON Format

```json
{
  "claim": "ABC123",
  "device": "espwifi-ABCD12",
  "tunnel": "ws_control"
}
```

**Most metadata, but:**
- Larger QR code
- Requires JSON parsing in app
- Overkill for simple pairing

## Device Implementation

### When to Display QR Code

```cpp
void setup() {
  espWiFi.begin();
  
  // Wait for cloud connection
  while (!espWiFi.cloud.isConnected()) {
    delay(100);
  }
  
  // Show QR code for initial pairing
  displayClaimCodeQR(espWiFi.cloud.getClaimCode());
}

void loop() {
  // Refresh QR code before expiration (every 9 minutes)
  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh > 9 * 60 * 1000) {
    displayClaimCodeQR(espWiFi.cloud.getClaimCode());
    lastRefresh = millis();
  }
}
```

### Display Options

#### 1. OLED/LCD Screen (Recommended)

```cpp
#include <Adafruit_SSD1306.h>
#include "qrcode.h"

void displayClaimCodeQR(const char* claimCode) {
  // Create URL for deep link
  char qrData[128];
  snprintf(qrData, sizeof(qrData), 
    "https://espwifi.io/claim?code=%s", claimCode);
  
  // Generate QR code
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, qrData);
  
  // Display on OLED
  display.clearDisplay();
  int scale = min(SCREEN_WIDTH / qrcode.size, 
                  SCREEN_HEIGHT / qrcode.size);
  
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.fillRect(x * scale, y * scale, scale, scale, WHITE);
      }
    }
  }
  
  // Show code below QR for manual entry fallback
  display.setCursor(0, SCREEN_HEIGHT - 10);
  display.printf("Code: %s", claimCode);
  display.display();
}
```

#### 2. Serial Console (Debug)

```cpp
void displayClaimCodeQR(const char* claimCode) {
  char qrData[128];
  snprintf(qrData, sizeof(qrData), 
    "https://espwifi.io/claim?code=%s", claimCode);
  
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, qrData);
  
  // ASCII QR code for serial monitor
  Serial.println("\n\n=== Scan to Pair ===");
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      Serial.print(qrcode_getModule(&qrcode, x, y) ? "██" : "  ");
    }
    Serial.println();
  }
  Serial.printf("Code: %s\n", claimCode);
  Serial.println("====================\n");
}
```

#### 3. Web Interface (Best for Remote Access)

```cpp
// Serve QR code as SVG at /qr endpoint
void handleQRCode(AsyncWebServerRequest *request) {
  const char* claimCode = espWiFi.cloud.getClaimCode();
  if (!claimCode) {
    request->send(404, "text/plain", "No claim code");
    return;
  }
  
  char qrData[128];
  snprintf(qrData, sizeof(qrData), 
    "https://espwifi.io/claim?code=%s", claimCode);
  
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, qrData);
  
  // Build SVG
  String svg = "<?xml version=\"1.0\"?>\n";
  svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
  svg += "viewBox=\"0 0 " + String(qrcode.size) + " " + String(qrcode.size) + "\">\n";
  svg += "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
  
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        svg += "<rect x=\"" + String(x) + "\" y=\"" + String(y) + 
               "\" width=\"1\" height=\"1\" fill=\"black\"/>\n";
      }
    }
  }
  svg += "</svg>";
  
  request->send(200, "image/svg+xml", svg);
}

// Register in setup()
server.on("/qr", HTTP_GET, handleQRCode);
```

## Dashboard Implementation

### QR Scanner in ClaimCodeEntry Component

```javascript
import { Html5QrcodeScanner } from "html5-qrcode";

function ClaimCodeEntry({ open, onDeviceClaimed }) {
  const [showScanner, setShowScanner] = useState(false);
  
  useEffect(() => {
    if (!showScanner) return;
    
    const scanner = new Html5QrcodeScanner("qr-reader", {
      fps: 10,
      qrbox: 250
    });
    
    scanner.render(onScanSuccess, onScanError);
    
    return () => scanner.clear();
  }, [showScanner]);
  
  const onScanSuccess = (decodedText) => {
    // Handle different QR formats
    
    // URL format: https://espwifi.io/claim?code=ABC123
    const urlMatch = decodedText.match(/code=([A-Z0-9]{6})/);
    if (urlMatch) {
      setClaimCode(urlMatch[1]);
      handleSubmit(urlMatch[1]);
      return;
    }
    
    // Simple format: ABC123
    if (/^[A-Z0-9]{6}$/.test(decodedText)) {
      setClaimCode(decodedText);
      handleSubmit(decodedText);
      return;
    }
    
    // JSON format: {"claim":"ABC123",...}
    try {
      const json = JSON.parse(decodedText);
      if (json.claim) {
        setClaimCode(json.claim);
        handleSubmit(json.claim);
        return;
      }
    } catch {}
    
    setError("Invalid QR code format");
  };
}
```

### Deep Link Handling

Add route to handle `/claim?code=ABC123` URLs:

```javascript
// In App.js or router
useEffect(() => {
  // Check URL for claim code
  const params = new URLSearchParams(window.location.search);
  const claimCode = params.get('code');
  
  if (claimCode && /^[A-Z0-9]{6}$/.test(claimCode)) {
    // Show claim code entry with pre-filled code
    setShowClaimCodeEntry(true);
    setPrefilledCode(claimCode);
    
    // Clear URL params
    window.history.replaceState({}, '', '/');
  }
}, []);
```

## Security Considerations

### Claim Code Properties

- ✅ **10-minute expiration** - Must be used quickly
- ✅ **One-time use** - Consumed on first redemption
- ✅ **Random generation** - Unpredictable
- ⚠️ **Not a secret** - Meant to be shared temporarily

### QR Code Safety

**Safe to display publicly:**
- Claim code expires in 10 minutes
- One-time use prevents replay attacks
- Device requires auth token for actual control

**Not safe:**
- Don't include auth token in QR code
- Don't use claim code as password
- Don't display QR indefinitely (refresh before expiration)

## User Experience

### Good UX Flow

1. **Device shows QR code** on screen/display
2. **User scans** with phone camera
3. **Browser opens** espwifi.io automatically
4. **Dashboard pairs** device automatically
5. **User controls** device immediately
6. **Future visits** - No QR needed (stored in browser)

### Bad UX Flow

❌ Device requires QR scan every time
❌ QR code changes randomly during use
❌ User must manually type URL from QR
❌ QR code expires during scanning

## Summary

✅ **QR codes are for initial pairing** - Not for already-paired browsers
✅ **URL format recommended** - Deep link to espwifi.io/claim
✅ **Display on device screen** - OLED/LCD or web interface
✅ **Refresh before expiration** - Every 9 minutes (10-min expiration)
✅ **localStorage stores credentials** - QR not needed after first pair
✅ **Safe to display publicly** - Claim codes expire and are one-time use

QR codes provide the smoothest pairing experience when combined with deep links and localStorage persistence!
