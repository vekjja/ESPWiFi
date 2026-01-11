# WebSocket Refactoring Summary

## Overview
Simplified the WebSocket architecture by separating generic WebSocket functionality from cloud tunnel logic.

## Changes Made

### 1. New Files Created

#### `include/CloudTunnel.h` & `src/CloudTunnel.cpp`
- **Purpose**: Handles cloud WebSocket tunneling to broker
- **Responsibilities**:
  - Connects to cloud broker (e.g., wss://tnl.espwifi.io)
  - Manages WebSocket client lifecycle
  - Forwards messages between cloud and local handlers
  - Tracks UI connection state
  - Synthetic client FD (`kCloudClientFd = -7777`) for routing

### 2. Simplified Files

#### `include/WebSocket.h` & `src/WebSocket.cpp` (Reduced from ~1350 to ~550 lines)
**Before**: Mixed generic WebSocket + cloud tunnel logic
**After**: Pure generic WebSocket wrapper

**Key Changes**:
- Removed all cloud tunnel code
- Removed ESPWiFi dependency (uses `void* userCtx` instead)
- Simplified callbacks: `OnMessageCb`, `OnConnectCb`, `OnDisconnectCb`
- Clean API:
  - `sendText(clientFd, msg, len)` - send to specific client
  - `sendBinary(clientFd, data, len)` - send binary to client
  - `broadcastText(msg, len)` - broadcast to all LAN clients
  - `broadcastBinary(data, len)` - broadcast binary to all
- Optional auth via callback: `AuthCheckFn`
- No route-specific knowledge (camera, control, etc.)

### 3. Updated Files

#### `src/ControlSocket.cpp`
**Purpose**: Coordinates control WebSocket + cloud tunnel

**Architecture**:
```
┌─────────────────────────────────────────┐
│  ControlSocket (static instance)       │
│                                         │
│  ┌─────────────┐    ┌───────────────┐ │
│  │  WebSocket  │    │ CloudTunnel   │ │
│  │  (LAN)      │    │ (Cloud)       │ │
│  └──────┬──────┘    └──────┬────────┘ │
│         │                  │           │
│         └──────┬───────────┘           │
│                ▼                        │
│         Message Handler                │
│         (ctrlOnMessage)                │
└─────────────────────────────────────────┘
```

**Flow**:
1. LAN clients connect to `WebSocket` at `/ws/control`
2. Cloud clients connect via `CloudTunnel` to broker
3. Both route through same `ctrlOnMessage` handler
4. Responses sent back via appropriate channel (LAN or cloud)

**Key Functions**:
- `ctrlOnMessage`: Handles commands from both LAN and cloud
- `cloudOnMessage`: CloudTunnel callback, forwards to ctrlOnMessage
- `cloudOnConnect/Disconnect`: CloudTunnel lifecycle callbacks

#### `src/Camera.cpp`
**Changes**:
- Use `ctrlSoc.sendBinary(fd, data, len)` for LAN clients
- Use `sendToCloudTunnel(data, len)` for cloud clients
- Check `CloudTunnel::kCloudClientFd` for cloud snapshots
- Use `cloudUIConnected()` instead of `ctrlSoc.cloudUIConnected()`

#### `include/ESPWiFi.h` & `src/Info.cpp` & `src/Config.cpp`
**New ESPWiFi methods**:
- `bool sendToCloudTunnel(data, len)` - Send binary to cloud
- `bool cloudTunnelEnabled()` - Check if tunnel enabled
- `bool cloudTunnelConnected()` - Check if tunnel connected
- `bool cloudUIConnected()` - Check if UI attached
- `const char* cloudUIWSURL()` - Get UI WebSocket URL
- `const char* cloudDeviceWSURL()` - Get device WebSocket URL
- `void syncCloudTunnelFromConfig()` - Reload cloud tunnel config

## Benefits

### 1. **Separation of Concerns**
- `WebSocket`: Generic LAN WebSocket handling
- `CloudTunnel`: Cloud broker connectivity
- `ControlSocket`: Application-specific routing

### 2. **Reusability**
WebSocket class can now be used for any endpoint without cloud knowledge:
```cpp
WebSocket mySocket;
mySocket.begin("/ws/custom", server, userCtx, onMsg, onConn, onDisc);
```

### 3. **Maintainability**
- Each class has a single, clear responsibility
- Easier to debug (check LAN vs cloud separately)
- Less coupling between components

### 4. **Testability**
- WebSocket can be tested without cloud infrastructure
- CloudTunnel can be tested independently
- ControlSocket orchestration can be unit tested

### 5. **Code Reduction**
- WebSocket.cpp: ~1350 → ~550 lines (-59%)
- CloudTunnel.cpp: 0 → ~420 lines (new, isolated)
- Total: More lines but better organized

## Cloud Tunnel Architecture

### Registration Flow
```
Device                  Cloud Broker              Dashboard
  │                          │                        │
  ├─ connect(announce=1) ──►│                        │
  │                          │                        │
  │◄─── "registered" ────────┤                        │
  │    (ui_ws_url,           │                        │
  │     device_ws_url)       │                        │
  │                          │                        │
  │                          │◄── connect(token) ─────┤
  │                          │                        │
  │◄── "ui_connected" ───────┤                        │
  │                          │                        │
  │                          │                        │
  ├─── camera frame ────────►├─── forward ──────────►│
  │                          │                        │
  │◄─── control cmd ─────────┤◄──── send ────────────┤
  │                          │                        │
```

### Message Routing
```
┌──────────────────────────────────────────────────┐
│  ctrlOnMessage(clientFd, type, data)            │
│                                                  │
│  if (clientFd == CloudTunnel::kCloudClientFd)   │
│      → Send response via CloudTunnel            │
│  else                                            │
│      → Send response via WebSocket              │
└──────────────────────────────────────────────────┘
```

## Backwards Compatibility

- **Frontend**: No changes needed (same WebSocket URLs)
- **Config**: Same JSON structure
- **Cloud Broker**: No changes needed
- **Camera Streaming**: Works as before

## Testing Checklist

- [ ] LAN control WebSocket commands
- [ ] Cloud tunnel registration
- [ ] Cloud control commands via tunnel
- [ ] Camera streaming over LAN
- [ ] Camera streaming over cloud tunnel
- [ ] Snapshot via LAN
- [ ] Snapshot via cloud
- [ ] Multiple LAN clients
- [ ] Multiple cloud UIs
- [ ] Config updates (enable/disable tunnel)
- [ ] Network interruptions/reconnects

## Future Improvements

1. **Multiple Tunnels**: Easy to add camera-specific tunnel
   ```cpp
   CloudTunnel cameraTunnel;
   cameraTunnel.configure(baseUrl, deviceId, token, "ws_camera");
   ```

2. **Custom Transports**: CloudTunnel interface could support other protocols
3. **Metrics**: Add connection stats, message counts, latency tracking
4. **Load Balancing**: Multiple broker URLs with failover
