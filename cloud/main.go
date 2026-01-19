package main

import (
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/gorilla/websocket"
)

type logLevel int

const (
	logInfo logLevel = iota
	logDebug
)

func parseLogLevel(s string) logLevel {
	switch strings.ToLower(strings.TrimSpace(s)) {
	case "debug":
		return logDebug
	default:
		return logInfo
	}
}

type deviceInfo struct {
	DeviceID   string    `json:"device_id"`
	TunnelKey  string    `json:"tunnel,omitempty"`
	Connected  bool      `json:"connected"`
	ConnectedAt time.Time `json:"connected_at,omitempty"`
	LastSeen   time.Time `json:"last_seen,omitempty"`
	UIWSURL    string    `json:"ui_ws_url"`
	DeviceWSURL string   `json:"device_ws_url"`
}

type hub struct {
	mu      sync.Mutex
	devices map[string]*deviceConn
}

type deviceConn struct {
	id         string
	ws         *websocket.Conn
	connectedAt time.Time
	lastSeen   atomic.Int64 // unix nanos

	// Gorilla websocket requires all writes to be serialized per connection.
	writeMu sync.Mutex

	// Paired UI websocket. Only one at a time for now.
	uiMu sync.Mutex
	uiConns   map[*websocket.Conn]struct{}
	uiWriteMu sync.Mutex // serializes writes across all UI conns

	// Device-provided auth token (used to authorize UI connections).
	// Typically this is the device's auth.token so the UI can connect securely.
	uiToken string

	// Closed when device is torn down.
	closed chan struct{}
}

func newHub() *hub {
	return &hub{devices: make(map[string]*deviceConn)}
}

func (h *hub) setDevice(id string, dc *deviceConn) (old *deviceConn) {
	h.mu.Lock()
	defer h.mu.Unlock()
	old = h.devices[id]
	h.devices[id] = dc
	return old
}

func (h *hub) getDevice(id string) *deviceConn {
	h.mu.Lock()
	defer h.mu.Unlock()
	return h.devices[id]
}

func (h *hub) deleteDevice(id string, dc *deviceConn) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if cur, ok := h.devices[id]; ok && cur == dc {
		delete(h.devices, id)
	}
}

func (h *hub) snapshot(publicBase string) []deviceInfo {
	h.mu.Lock()
	defer h.mu.Unlock()

	out := make([]deviceInfo, 0, len(h.devices))
	now := time.Now()
	_ = now
	for key, dc := range h.devices {
		devID, tunnel := splitKey(key)
		last := time.Unix(0, dc.lastSeen.Load())
		ui := strings.TrimRight(publicBase, "/") + "/ws/ui/" + devID
		dev := strings.TrimRight(publicBase, "/") + "/ws/device/" + devID
		if tunnel != "" {
			ui += "?tunnel=" + urlQueryEscape(tunnel)
			dev += "?tunnel=" + urlQueryEscape(tunnel)
		}
		out = append(out, deviceInfo{
			DeviceID:    devID,
			TunnelKey:   tunnel,
			Connected:   dc.ws != nil,
			ConnectedAt: dc.connectedAt,
			LastSeen:    last,
			UIWSURL:     ui,
			DeviceWSURL: dev,
		})
	}
	return out
}

type server struct {
	h *hub

	// Optional global auth gates (kept for backwards compatibility).
	// If unset, the device can still provide its own per-device token at
	// registration time (?token=... / Authorization: Bearer ...), which is then
	// required for UI websocket connections.
	deviceAuthToken string
	uiAuthToken     string

	// If set, used to build public URLs; otherwise inferred from request headers.
	publicBaseURL string

	upgrader websocket.Upgrader

	logLevel   logLevel
	logHealthz bool

	// Claim codes: short-lived one-time codes used to exchange for the device's
	// long auth token (so iOS users can pair without handling the token in BLE tools).
	claimMu sync.Mutex
	claims  map[string]claimEntry
}

type claimEntry struct {
	DeviceID   string
	TunnelKey  string
	Token      string
	ExpiresAt  time.Time
	Registered time.Time
}

func main() {
	var (
		listenAddr = flag.String("listen", envOr("LISTEN_ADDR", ":8080"), "listen address")
		publicBase = flag.String("public-base-url", envOr("PUBLIC_BASE_URL", ""), "public base URL used to generate ws URLs (e.g. https://tunnel.example.com)")
	)
	flag.Parse()

	s := &server{
		h:               newHub(),
		deviceAuthToken: os.Getenv("DEVICE_AUTH_TOKEN"),
		uiAuthToken:     os.Getenv("UI_AUTH_TOKEN"),
		publicBaseURL:   *publicBase,
		logLevel:        parseLogLevel(envOr("LOG_LEVEL", "info")),
		logHealthz:      envOr("LOG_HEALTHZ", "0") == "1",
		claims:          make(map[string]claimEntry),
		upgrader: websocket.Upgrader{
			ReadBufferSize:  32 * 1024,
			WriteBufferSize: 32 * 1024,
			CheckOrigin: func(r *http.Request) bool {
				// Expect to run behind a reverse proxy/ingress; origin checks should be enforced there.
				return true
			},
		},
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", s.handleHealthz)
	mux.HandleFunc("/api/register", s.handleRegister)
	mux.HandleFunc("/api/devices", s.handleDevices)
	mux.HandleFunc("/api/claim", s.handleClaim)
	mux.HandleFunc("/ws/device/", s.handleDeviceWS)
	mux.HandleFunc("/ws/ui/", s.handleUIWS)

	httpSrv := &http.Server{
		Addr:              *listenAddr,
		Handler:           loggingMiddleware(mux, s),
		ReadHeaderTimeout: 10 * time.Second,
	}

	go func() {
		log.Printf("ESPWiFi Cloud ☁️ Listening on %s", *listenAddr)
		if err := httpSrv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatalf("ListenAndServe: %v", err)
		}
	}()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	<-ctx.Done()
	stop()

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	_ = httpSrv.Shutdown(shutdownCtx)
}

func (s *server) handleHealthz(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{"ok": true})
}

func (s *server) setCORS(w http.ResponseWriter, r *http.Request) {
	// This service is intended to be called by espwifi.io and local dashboards.
	// Keep this permissive for now; origin enforcement should happen at ingress.
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
	// Avoid caching claim responses.
	w.Header().Set("Cache-Control", "no-store")
}

type claimRequest struct {
	Code   string `json:"code"`
	Tunnel string `json:"tunnel,omitempty"`
}

func (s *server) handleClaim(w http.ResponseWriter, r *http.Request) {
	s.setCORS(w, r)
	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req claimRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "bad json", http.StatusBadRequest)
		return
	}
	code := strings.ToUpper(strings.TrimSpace(req.Code))
	if code == "" || len(code) > 32 {
		http.Error(w, "invalid code", http.StatusBadRequest)
		return
	}
	tunnel := strings.TrimSpace(req.Tunnel)
	if tunnel == "" {
		tunnel = "ws_control"
	}

	now := time.Now().UTC()

	s.claimMu.Lock()
	ce, ok := s.claims[code]
	if ok {
		// Enforce tunnel match if provided. (Allows future per-tunnel claims.)
		if ce.TunnelKey != "" && tunnel != "" && ce.TunnelKey != tunnel {
			ok = false
		}
		if ok && now.After(ce.ExpiresAt) {
			delete(s.claims, code)
			ok = false
		}
	}
	if ok {
		// One-time use: consume immediately.
		delete(s.claims, code)
	}
	s.claimMu.Unlock()

	if !ok || ce.DeviceID == "" || ce.Token == "" {
		http.Error(w, "invalid or expired code", http.StatusNotFound)
		s.logf(logInfo, "claim_invalid", "remote", clientIP(r), "code", code)
		return
	}

	publicBase := s.publicBase(r)
	ui := strings.TrimRight(publicBase, "/") + "/ws/ui/" + ce.DeviceID + "?tunnel=" + urlQueryEscape(tunnel)
	// Provide token as both a field and embedded in the url for convenience.
	uiWithToken := ui + "&token=" + urlQueryEscape(ce.Token)

	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(map[string]any{
		"ok":          true,
		"code":        code,
		"device_id":   ce.DeviceID,
		"tunnel":      tunnel,
		"ui_ws_url":   ui,
		"token":       ce.Token,
		"ui_ws_token": uiWithToken,
	})

	s.logf(logInfo, "claim_redeemed",
		"remote", clientIP(r),
		"device_id", ce.DeviceID,
		"tunnel", tunnel,
	)
}

type registerRequest struct {
	DeviceID string `json:"device_id"`
}

func (s *server) handleRegister(w http.ResponseWriter, r *http.Request) {
	// Simple helper endpoint for dashboards to discover the ws URLs.
	// It does NOT create a device session; the device must still connect to /ws/device/{id}.
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var req registerRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid json", http.StatusBadRequest)
		return
	}
	req.DeviceID = strings.TrimSpace(req.DeviceID)
	if req.DeviceID == "" || strings.Contains(req.DeviceID, "/") {
		http.Error(w, "invalid device_id", http.StatusBadRequest)
		return
	}
	tunnel := strings.TrimSpace(r.URL.Query().Get("tunnel"))
	if strings.Contains(tunnel, "/") {
		http.Error(w, "invalid tunnel", http.StatusBadRequest)
		return
	}

	publicBase := s.publicBase(r)
	ui := strings.TrimRight(publicBase, "/") + "/ws/ui/" + req.DeviceID
	dev := strings.TrimRight(publicBase, "/") + "/ws/device/" + req.DeviceID
	if tunnel != "" {
		ui += "?tunnel=" + urlQueryEscape(tunnel)
		dev += "?tunnel=" + urlQueryEscape(tunnel)
	}
	info := deviceInfo{
		DeviceID:    req.DeviceID,
		TunnelKey:   tunnel,
		Connected:   s.h.getDevice(makeKey(req.DeviceID, tunnel)) != nil,
		UIWSURL:     ui,
		DeviceWSURL: dev,
	}
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(info)
}

func (s *server) handleDevices(w http.ResponseWriter, r *http.Request) {
	publicBase := s.publicBase(r)
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(s.h.snapshot(publicBase))
}

func (s *server) handleDeviceWS(w http.ResponseWriter, r *http.Request) {
	deviceID := strings.TrimPrefix(r.URL.Path, "/ws/device/")
	deviceID = strings.Trim(deviceID, "/")
	if deviceID == "" || strings.Contains(deviceID, "/") {
		http.Error(w, "invalid device id", http.StatusBadRequest)
		s.logf(logInfo, "device_ws_invalid_device_id", "remote", clientIP(r), "path", r.URL.Path)
		return
	}
	tunnel := strings.TrimSpace(r.URL.Query().Get("tunnel"))
	if strings.Contains(tunnel, "/") {
		http.Error(w, "invalid tunnel", http.StatusBadRequest)
		s.logf(logInfo, "device_ws_invalid_tunnel", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
		return
	}

	claim := strings.ToUpper(strings.TrimSpace(r.URL.Query().Get("claim")))
	if len(claim) > 0 && len(claim) > 32 {
		http.Error(w, "invalid claim", http.StatusBadRequest)
		s.logf(logInfo, "device_ws_invalid_claim", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
		return
	}

	if s.deviceAuthToken != "" && !authOK(r, s.deviceAuthToken) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		s.logf(logInfo, "device_ws_unauthorized_global", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
		return
	}

	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}

	// Capture per-device UI token (device provides it during registration).
	// This is used to authorize /ws/ui connections for this device.
	deviceProvidedToken := extractToken(r)

	dc := &deviceConn{
		id:          makeKey(deviceID, tunnel),
		ws:          conn,
		connectedAt: time.Now().UTC(),
		closed:      make(chan struct{}),
		uiToken:     deviceProvidedToken,
		uiConns:     make(map[*websocket.Conn]struct{}),
	}
	dc.lastSeen.Store(time.Now().UTC().UnixNano())

	// Replace any existing device session.
	key := makeKey(deviceID, tunnel)
	if old := s.h.setDevice(key, dc); old != nil {
		s.logf(logInfo, "device_ws_replaced", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
		old.closeWithReason(websocket.ClosePolicyViolation, "replaced by new device connection")
		s.h.deleteDevice(key, old)
	}

	s.logf(logInfo, "device_ws_connected",
		"remote", clientIP(r),
		"device_id", deviceID,
		"tunnel", tunnel,
		"ui_token_present", dc.uiToken != "",
	)

	publicBase := s.publicBase(r)
	if r.URL.Query().Get("announce") == "1" {
		ui := strings.TrimRight(publicBase, "/") + "/ws/ui/" + deviceID
		dev := strings.TrimRight(publicBase, "/") + "/ws/device/" + deviceID
		if tunnel != "" {
			ui += "?tunnel=" + urlQueryEscape(tunnel)
			dev += "?tunnel=" + urlQueryEscape(tunnel)
		}
		_ = dc.ws.WriteMessage(websocket.TextMessage, mustJSON(map[string]any{
			"type":         "registered",
			"device_id":    deviceID,
			"tunnel":       tunnel,
			"ui_ws_url":    ui,
			"device_ws_url": dev,
			// Hint for clients: UI must present the token the device provided when
			// connecting to the tunnel (typically auth.token).
			"ui_token_required": dc.uiToken != "",
		}))
		s.logf(logDebug, "device_ws_registered", "device_id", deviceID, "tunnel", tunnel, "ui_token_required", dc.uiToken != "", "ui_ws_url", ui)
	}

	// If device presented a claim code, store it as short-lived one-time.
	if claim != "" && dc.uiToken != "" {
		now := time.Now().UTC()
		s.claimMu.Lock()
		s.claims[claim] = claimEntry{
			DeviceID:   deviceID,
			TunnelKey:  tunnel,
			Token:      dc.uiToken,
			ExpiresAt:  now.Add(10 * time.Minute),
			Registered: now,
		}
		s.claimMu.Unlock()
		s.logf(logInfo, "device_claim_registered", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel, "claim", claim)
	}

	// Keepalive/read loop: we don't interpret payloads here; we just maintain the device session.
	// IMPORTANT: Gorilla websockets do not allow concurrent readers or concurrent writers.
	// We keep exactly one reader for the device connection here, and forward to the UI if paired.
	conn.SetReadLimit(8 << 20) // 8MB per message
	_ = conn.SetReadDeadline(time.Now().Add(120 * time.Second))
	conn.SetPongHandler(func(string) error {
		dc.lastSeen.Store(time.Now().UTC().UnixNano())
		_ = conn.SetReadDeadline(time.Now().Add(120 * time.Second))
		return nil
	})

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	type wsMsg struct {
		mt  int
		msg []byte
	}
	msgCh := make(chan wsMsg, 8)
	errCh := make(chan error, 1)
	go func() {
		for {
			mt, msg, err := conn.ReadMessage()
			dc.lastSeen.Store(time.Now().UTC().UnixNano())
			if err != nil {
				errCh <- err
				return
			}
			// Best-effort forward to UI via main loop (single writer there).
			select {
			case msgCh <- wsMsg{mt: mt, msg: msg}:
			default:
				// Drop if UI can't keep up; avoid blocking device reader.
			}
		}
	}()

	for {
		select {
		case <-dc.closed:
			s.h.deleteDevice(key, dc)
			s.logf(logInfo, "device_ws_disconnected", "device_id", deviceID, "tunnel", tunnel)
			return
		case err := <-errCh:
			// Bubble up the disconnect cause to make flapping debuggable.
			errMsg := ""
			if err != nil {
				errMsg = err.Error()
			}
			dc.closeWithReason(websocket.CloseNormalClosure, "device disconnected")
			s.h.deleteDevice(key, dc)
			s.logf(logInfo, "device_ws_disconnected", "device_id", deviceID, "tunnel", tunnel, "err", errMsg)
			return
		case m := <-msgCh:
			// Forward device payload to any connected UI clients.
			dc.uiMu.Lock()
			uis := make([]*websocket.Conn, 0, len(dc.uiConns))
			for c := range dc.uiConns {
				uis = append(uis, c)
			}
			dc.uiMu.Unlock()
			if len(uis) > 0 {
				dc.uiWriteMu.Lock()
				for _, uiConn := range uis {
					_ = uiConn.WriteMessage(m.mt, m.msg)
				}
				dc.uiWriteMu.Unlock()
			}
		case <-ticker.C:
			dc.writeMu.Lock()
			_ = conn.WriteControl(websocket.PingMessage, []byte("ping"), time.Now().Add(5*time.Second))
			dc.writeMu.Unlock()
		}
	}
}

func isWSUpgrade(r *http.Request) bool {
	if r == nil {
		return false
	}
	if !strings.EqualFold(r.Header.Get("Upgrade"), "websocket") {
		return false
	}
	// Connection may contain multiple tokens (e.g. "keep-alive, Upgrade")
	conn := r.Header.Get("Connection")
	for _, tok := range strings.Split(conn, ",") {
		if strings.EqualFold(strings.TrimSpace(tok), "upgrade") {
			return true
		}
	}
	return false
}

// rejectWS attempts to upgrade so the client receives a proper WebSocket close
// frame (with reason). If upgrade is not possible, falls back to HTTP error.
func (s *server) rejectWS(w http.ResponseWriter, r *http.Request, httpStatus int, closeCode int, reason string, logKey string, kv ...any) {
	if isWSUpgrade(r) {
		c, err := s.upgrader.Upgrade(w, r, nil)
		if err == nil && c != nil {
			_ = c.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(closeCode, reason), time.Now().Add(3*time.Second))
			_ = c.Close()
			s.logf(logInfo, logKey, kv...)
			return
		}
	}
	http.Error(w, reason, httpStatus)
	s.logf(logInfo, logKey, kv...)
}

func (s *server) handleUIWS(w http.ResponseWriter, r *http.Request) {
	deviceID := strings.TrimPrefix(r.URL.Path, "/ws/ui/")
	deviceID = strings.Trim(deviceID, "/")
	if deviceID == "" || strings.Contains(deviceID, "/") {
		http.Error(w, "invalid device id", http.StatusBadRequest)
		s.logf(logInfo, "ui_ws_invalid_device_id", "remote", clientIP(r), "path", r.URL.Path)
		return
	}
	tunnel := strings.TrimSpace(r.URL.Query().Get("tunnel"))
	if strings.Contains(tunnel, "/") {
		http.Error(w, "invalid tunnel", http.StatusBadRequest)
		s.logf(logInfo, "ui_ws_invalid_tunnel", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
		return
	}

	if s.uiAuthToken != "" && !authOK(r, s.uiAuthToken) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		s.logf(logInfo, "ui_ws_unauthorized_global", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
		return
	}

	key := makeKey(deviceID, tunnel)
	dc := s.h.getDevice(key)
	if dc == nil {
		s.rejectWS(w, r, http.StatusNotFound, websocket.CloseTryAgainLater, "device_offline", "ui_ws_device_offline",
			"remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
		return
	}

	// Per-device UI token gate: if the device provided a token at registration,
	// require the UI to present the same token (?token=... or Bearer ...).
	if dc.uiToken != "" {
		got := extractToken(r)
		if subtle.ConstantTimeCompare([]byte(got), []byte(dc.uiToken)) != 1 {
			// Policy: upgrade+close so browsers can surface a reason (otherwise it looks like a generic 1006).
			s.rejectWS(w, r, http.StatusUnauthorized, websocket.ClosePolicyViolation, "unauthorized_device", "ui_ws_unauthorized_device",
				"remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
			return
		}
	}

	uiConn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}

	s.logf(logInfo, "ui_ws_connected", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)

	// Register this UI connection. Allow multiple UI clients per device+tunnel
	// (useful for multiple tabs + CLI tests).
	dc.uiMu.Lock()
	wasEmpty := len(dc.uiConns) == 0
	dc.uiConns[uiConn] = struct{}{}
	dc.uiMu.Unlock()
	if wasEmpty {
		// Tell the device a UI is attached so it can start streaming only when needed.
		dc.writeMu.Lock()
		_ = dc.ws.WriteMessage(websocket.TextMessage, []byte(`{"type":"ui_connected"}`))
		dc.writeMu.Unlock()
	}

	bridge(dc, uiConn)

	// UI disconnected; if this was the last UI, tell device it can stop streaming.
	dc.uiMu.Lock()
	delete(dc.uiConns, uiConn)
	nowEmpty := len(dc.uiConns) == 0
	dc.uiMu.Unlock()

	if nowEmpty {
		dc.writeMu.Lock()
		_ = dc.ws.WriteMessage(websocket.TextMessage, []byte(`{"type":"ui_disconnected"}`))
		dc.writeMu.Unlock()
	}
	s.logf(logInfo, "ui_ws_disconnected", "remote", clientIP(r), "device_id", deviceID, "tunnel", tunnel)
}

func makeKey(deviceID, tunnel string) string {
	deviceID = strings.TrimSpace(deviceID)
	tunnel = strings.TrimSpace(tunnel)
	if tunnel == "" {
		return deviceID
	}
	return deviceID + "|" + tunnel
}

func splitKey(key string) (deviceID, tunnel string) {
	if i := strings.IndexByte(key, '|'); i >= 0 {
		return key[:i], key[i+1:]
	}
	return key, ""
}

func urlQueryEscape(s string) string {
	// Minimal query escaping for tunnel keys; avoid importing net/url just for this.
	// Safe for alphanumerics, '-', '_', '.', '~'. Everything else is %XX.
	var b strings.Builder
	b.Grow(len(s))
	for i := 0; i < len(s); i++ {
		c := s[i]
		if (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~' {
			b.WriteByte(c)
		} else {
			const hex = "0123456789ABCDEF"
			b.WriteByte('%')
			b.WriteByte(hex[c>>4])
			b.WriteByte(hex[c&15])
		}
	}
	return b.String()
}

func bridge(dc *deviceConn, uiConn *websocket.Conn) {
	deviceConn := dc.ws

	// Configure UI read limit. Device reads are handled by handleDeviceWS (single reader).
	uiConn.SetReadLimit(8 << 20)

	// Forward: UI -> Device (serialize writes to deviceConn).
	for {
		mt, msg, err := uiConn.ReadMessage()
		if err != nil {
			return
		}
		dc.lastSeen.Store(time.Now().UTC().UnixNano())
		dc.writeMu.Lock()
		werr := deviceConn.WriteMessage(mt, msg)
		dc.writeMu.Unlock()
		if werr != nil {
			return
		}
	}
}

func (dc *deviceConn) closeWithReason(code int, reason string) {
	select {
	case <-dc.closed:
		// already closed
	default:
		close(dc.closed)
	}
	dc.writeMu.Lock()
	_ = dc.ws.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(code, reason), time.Now().Add(3*time.Second))
	_ = dc.ws.Close()
	dc.writeMu.Unlock()

	dc.uiMu.Lock()
	uis := make([]*websocket.Conn, 0, len(dc.uiConns))
	for c := range dc.uiConns {
		uis = append(uis, c)
	}
	dc.uiConns = make(map[*websocket.Conn]struct{})
	dc.uiMu.Unlock()

	if len(uis) > 0 {
		dc.uiWriteMu.Lock()
		for _, c := range uis {
			_ = c.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(code, reason), time.Now().Add(3*time.Second))
			_ = c.Close()
		}
		dc.uiWriteMu.Unlock()
	}
}

func (s *server) publicBase(r *http.Request) string {
	var base string
	if strings.TrimSpace(s.publicBaseURL) != "" {
		base = strings.TrimRight(strings.TrimSpace(s.publicBaseURL), "/")
	} else {
		// Infer from reverse-proxy headers when available.
		proto := r.Header.Get("X-Forwarded-Proto")
		if proto == "" {
			if r.TLS != nil {
				proto = "https"
			} else {
				proto = "https" // Force HTTPS even if not detected
			}
		}
		host := r.Header.Get("X-Forwarded-Host")
		if host == "" {
			host = r.Host
		}
		base = proto + "://" + host
	}

	// Convert https:// -> wss:// for WebSocket URLs (only support secure connections)
	if strings.HasPrefix(base, "https://") {
		return "wss://" + strings.TrimPrefix(base, "https://")
	}
	
	// If someone configured http://, reject it - we only support secure connections
	if strings.HasPrefix(base, "http://") {
		// Log a warning but still upgrade to wss for security
		return "wss://" + strings.TrimPrefix(base, "http://")
	}
	
	// Already wss:// or unknown format
	return base
}

func authOK(r *http.Request, token string) bool {
	// Supports either:
	// - Authorization: Bearer <token>
	// - ?token=<token>
	got := ""
	if ah := r.Header.Get("Authorization"); ah != "" {
		const pfx = "Bearer "
		if strings.HasPrefix(ah, pfx) {
			got = strings.TrimSpace(strings.TrimPrefix(ah, pfx))
		}
	}
	if got == "" {
		got = r.URL.Query().Get("token")
	}
	if got == "" {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(got), []byte(token)) == 1
}

func extractToken(r *http.Request) string {
	// Supports either:
	// - Authorization: Bearer <token>
	// - ?token=<token>
	got := ""
	if ah := r.Header.Get("Authorization"); ah != "" {
		const pfx = "Bearer "
		if strings.HasPrefix(ah, pfx) {
			got = strings.TrimSpace(strings.TrimPrefix(ah, pfx))
		}
	}
	if got == "" {
		got = r.URL.Query().Get("token")
	}
	return got
}

func mustJSON(v any) []byte {
	b, _ := json.Marshal(v)
	return b
}

func envOr(k, def string) string {
	if v := strings.TrimSpace(os.Getenv(k)); v != "" {
		return v
	}
	return def
}

type statusCapturingResponseWriter struct {
	http.ResponseWriter
	status int
	bytes  int
}

func (w *statusCapturingResponseWriter) WriteHeader(statusCode int) {
	w.status = statusCode
	w.ResponseWriter.WriteHeader(statusCode)
}

func (w *statusCapturingResponseWriter) Write(p []byte) (int, error) {
	if w.status == 0 {
		w.status = http.StatusOK
	}
	n, err := w.ResponseWriter.Write(p)
	w.bytes += n
	return n, err
}

func loggingMiddleware(next http.Handler, s *server) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		// IMPORTANT: Don't wrap ResponseWriter for websocket upgrade requests.
		// Gorilla's Upgrader requires http.Hijacker (and friends) and will fail
		// if we hide those interfaces behind a wrapper.
		if isWebSocketRequest(r) {
			// Let the handler run; websocket handlers log their own lifecycle events.
			next.ServeHTTP(w, r)
			return
		}
		if r.URL.Path == "/healthz" && s != nil && !s.logHealthz {
			next.ServeHTTP(w, r)
			return
		}
		start := time.Now()
		sw := &statusCapturingResponseWriter{ResponseWriter: w}
		next.ServeHTTP(sw, r)
		dur := time.Since(start)

		remote := clientIP(r)
		status := sw.status
		if status == 0 {
			status = http.StatusOK
		}
		log.Printf("%s %s %s %s %d %dB (%s)", remote, r.Method, r.URL.Path, r.Proto, status, sw.bytes, dur)
	})
}

func isWebSocketRequest(r *http.Request) bool {
	if r == nil {
		return false
	}
	// Header-based detection
	if strings.EqualFold(strings.TrimSpace(r.Header.Get("Upgrade")), "websocket") {
		return true
	}
	// Path-based fallback for proxies that don't preserve Upgrade header in logs
	return strings.HasPrefix(r.URL.Path, "/ws/")
}

func clientIP(r *http.Request) string {
	// Prefer reverse-proxy headers.
	if xff := strings.TrimSpace(r.Header.Get("X-Forwarded-For")); xff != "" {
		// first is original
		if i := strings.IndexByte(xff, ','); i >= 0 {
			return strings.TrimSpace(xff[:i])
		}
		return xff
	}
	if xr := strings.TrimSpace(r.Header.Get("X-Real-Ip")); xr != "" {
		return xr
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err == nil && host != "" {
		return host
	}
	return r.RemoteAddr
}

func (s *server) logf(level logLevel, event string, kv ...any) {
	if s == nil {
		return
	}
	if level == logDebug && s.logLevel != logDebug {
		return
	}
	var b strings.Builder
	b.Grow(128)
	b.WriteString(event)
	for i := 0; i+1 < len(kv); i += 2 {
		k, _ := kv[i].(string)
		v := kv[i+1]
		if k == "" {
			continue
		}
		b.WriteByte(' ')
		b.WriteString(k)
		b.WriteByte('=')
		b.WriteString(fmtAny(v))
	}
	log.Print(b.String())
}

func fmtAny(v any) string {
	switch t := v.(type) {
	case string:
		if t == "" {
			return `""`
		}
		// avoid logging extremely long values
		if len(t) > 256 {
			return t[:256] + "…"
		}
		return t
	case bool:
		if t {
			return "true"
		}
		return "false"
	case int:
		return strconv.Itoa(t)
	case int64:
		return strconv.FormatInt(t, 10)
	default:
		return fmt.Sprint(v)
	}
}