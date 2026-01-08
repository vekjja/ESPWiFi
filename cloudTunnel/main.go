package main

import (
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"flag"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/gorilla/websocket"
)

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

	// Paired UI websocket. Only one at a time for now.
	uiMu sync.Mutex
	ui   *websocket.Conn

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

	deviceAuthToken string
	uiAuthToken     string

	// If set, used to build public URLs; otherwise inferred from request headers.
	publicBaseURL string

	upgrader websocket.Upgrader
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
	mux.HandleFunc("/ws/device/", s.handleDeviceWS)
	mux.HandleFunc("/ws/ui/", s.handleUIWS)

	httpSrv := &http.Server{
		Addr:              *listenAddr,
		Handler:           loggingMiddleware(mux),
		ReadHeaderTimeout: 10 * time.Second,
	}

	go func() {
		log.Printf("cloudTunnel listening on %s", *listenAddr)
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
		return
	}
	tunnel := strings.TrimSpace(r.URL.Query().Get("tunnel"))
	if strings.Contains(tunnel, "/") {
		http.Error(w, "invalid tunnel", http.StatusBadRequest)
		return
	}

	if s.deviceAuthToken != "" && !authOK(r, s.deviceAuthToken) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}

	dc := &deviceConn{
		id:          makeKey(deviceID, tunnel),
		ws:          conn,
		connectedAt: time.Now().UTC(),
		closed:      make(chan struct{}),
	}
	dc.lastSeen.Store(time.Now().UTC().UnixNano())

	// Replace any existing device session.
	key := makeKey(deviceID, tunnel)
	if old := s.h.setDevice(key, dc); old != nil {
		old.closeWithReason(websocket.ClosePolicyViolation, "replaced by new device connection")
		s.h.deleteDevice(key, old)
	}

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
		}))
	}

	// Keepalive/read loop: we don't interpret payloads here; we just maintain the device session.
	// If a UI is connected, forwarding is handled by the pairing bridge.
	conn.SetReadLimit(8 << 20) // 8MB per message
	_ = conn.SetReadDeadline(time.Now().Add(120 * time.Second))
	conn.SetPongHandler(func(string) error {
		dc.lastSeen.Store(time.Now().UTC().UnixNano())
		_ = conn.SetReadDeadline(time.Now().Add(120 * time.Second))
		return nil
	})

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	errCh := make(chan error, 1)
	go func() {
		for {
			// If we're paired, the bridge goroutine will be doing reads; avoid double-reading.
			dc.uiMu.Lock()
			paired := dc.ui != nil
			dc.uiMu.Unlock()
			if paired {
				time.Sleep(200 * time.Millisecond)
				select {
				case <-dc.closed:
					errCh <- nil
					return
				default:
				}
				continue
			}

			_, _, err := conn.ReadMessage()
			dc.lastSeen.Store(time.Now().UTC().UnixNano())
			if err != nil {
				errCh <- err
				return
			}
		}
	}()

	for {
		select {
		case <-dc.closed:
			s.h.deleteDevice(key, dc)
			return
		case err := <-errCh:
			_ = err
			dc.closeWithReason(websocket.CloseNormalClosure, "device disconnected")
			s.h.deleteDevice(key, dc)
			return
		case <-ticker.C:
			_ = conn.WriteControl(websocket.PingMessage, []byte("ping"), time.Now().Add(5*time.Second))
		}
	}
}

func (s *server) handleUIWS(w http.ResponseWriter, r *http.Request) {
	deviceID := strings.TrimPrefix(r.URL.Path, "/ws/ui/")
	deviceID = strings.Trim(deviceID, "/")
	if deviceID == "" || strings.Contains(deviceID, "/") {
		http.Error(w, "invalid device id", http.StatusBadRequest)
		return
	}
	tunnel := strings.TrimSpace(r.URL.Query().Get("tunnel"))
	if strings.Contains(tunnel, "/") {
		http.Error(w, "invalid tunnel", http.StatusBadRequest)
		return
	}

	if s.uiAuthToken != "" && !authOK(r, s.uiAuthToken) {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	key := makeKey(deviceID, tunnel)
	dc := s.h.getDevice(key)
	if dc == nil {
		http.Error(w, "device offline", http.StatusNotFound)
		return
	}

	uiConn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}

	// Enforce a single UI client per device (new connection wins).
	dc.uiMu.Lock()
	oldUI := dc.ui
	dc.ui = uiConn
	dc.uiMu.Unlock()
	if oldUI != nil {
		_ = oldUI.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(websocket.ClosePolicyViolation, "replaced by another ui connection"), time.Now().Add(3*time.Second))
		_ = oldUI.Close()
	}

	bridge(dc, uiConn)
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

	// Configure timeouts.
	deviceConn.SetReadLimit(8 << 20)
	uiConn.SetReadLimit(8 << 20)

	_ = deviceConn.SetReadDeadline(time.Now().Add(120 * time.Second))
	_ = uiConn.SetReadDeadline(time.Now().Add(120 * time.Second))

	deviceConn.SetPongHandler(func(string) error {
		dc.lastSeen.Store(time.Now().UTC().UnixNano())
		_ = deviceConn.SetReadDeadline(time.Now().Add(120 * time.Second))
		return nil
	})
	uiConn.SetPongHandler(func(string) error {
		_ = uiConn.SetReadDeadline(time.Now().Add(120 * time.Second))
		return nil
	})

	done := make(chan struct{})
	defer close(done)

	// Forward: UI -> Device
	go func() {
		defer func() { _ = deviceConn.Close() }()
		for {
			mt, msg, err := uiConn.ReadMessage()
			if err != nil {
				return
			}
			dc.lastSeen.Store(time.Now().UTC().UnixNano())
			if err := deviceConn.WriteMessage(mt, msg); err != nil {
				return
			}
		}
	}()

	// Forward: Device -> UI
	go func() {
		defer func() { _ = uiConn.Close() }()
		for {
			mt, msg, err := deviceConn.ReadMessage()
			if err != nil {
				return
			}
			dc.lastSeen.Store(time.Now().UTC().UnixNano())
			if err := uiConn.WriteMessage(mt, msg); err != nil {
				return
			}
		}
	}()

	// Keepalive until either side closes.
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-dc.closed:
			return
		case <-ticker.C:
			_ = uiConn.WriteControl(websocket.PingMessage, []byte("ping"), time.Now().Add(5*time.Second))
			_ = deviceConn.WriteControl(websocket.PingMessage, []byte("ping"), time.Now().Add(5*time.Second))
		case <-time.After(200 * time.Millisecond):
			// Poll for closure via connection errors indirectly; goroutines will close sockets.
			dc.uiMu.Lock()
			cur := dc.ui
			dc.uiMu.Unlock()
			if cur != uiConn {
				return
			}
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
	_ = dc.ws.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(code, reason), time.Now().Add(3*time.Second))
	_ = dc.ws.Close()

	dc.uiMu.Lock()
	if dc.ui != nil {
		_ = dc.ui.WriteControl(websocket.CloseMessage, websocket.FormatCloseMessage(code, reason), time.Now().Add(3*time.Second))
		_ = dc.ui.Close()
		dc.ui = nil
	}
	dc.uiMu.Unlock()
}

func (s *server) publicBase(r *http.Request) string {
	if strings.TrimSpace(s.publicBaseURL) != "" {
		return strings.TrimRight(strings.TrimSpace(s.publicBaseURL), "/")
	}

	// Infer from reverse-proxy headers when available.
	proto := r.Header.Get("X-Forwarded-Proto")
	if proto == "" {
		if r.TLS != nil {
			proto = "https"
		} else {
			proto = "http"
		}
	}
	host := r.Header.Get("X-Forwarded-Host")
	if host == "" {
		host = r.Host
	}

	// Convert http(s) -> ws(s) for convenience.
	switch strings.ToLower(proto) {
	case "https":
		return "wss://" + host
	case "http":
		return "ws://" + host
	default:
		return "ws://" + host
	}
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

func loggingMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		next.ServeHTTP(w, r)
		dur := time.Since(start)

		host, _, _ := net.SplitHostPort(r.RemoteAddr)
		if host == "" {
			host = r.RemoteAddr
		}
		log.Printf("%s %s %s %s (%s)", host, r.Method, r.URL.Path, r.Proto, dur)
	})
}