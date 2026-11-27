// One WebSocket lifecycle for the dashboard.
// Callers describe the URL and what to do with messages. This module owns
// connect, backoff, offline, and "the socket came back" so those rules stay
// the same for the control socket, camera stream, and WebSocket cards.

const DEFAULT_INITIAL_MS = 600;
const DEFAULT_MAX_MS = 15000;
const DEFAULT_FACTOR = 1.7;

export const SOCKET_STATUS = {
  CONNECTING: "connecting",
  CONNECTED: "connected",
  RECONNECTING: "reconnecting",
  OFFLINE: "offline",
  DISCONNECTED: "disconnected",
  ERROR: "error",
  AUTH: "auth",
};

// Firmware rejects a bad token with close code 1008 and reason "unauthorized".
// 1006 is an abnormal network drop and must keep retrying.
const AUTH_CLOSE_CODE = 1008;

function isAuthClose(event) {
  if (!event) return false;
  if (event.code === AUTH_CLOSE_CODE) return true;
  const reason = String(event.reason || "").toLowerCase();
  return reason.includes("unauthorized");
}

function browserOnline() {
  if (typeof navigator === "undefined") return true;
  return navigator.onLine !== false;
}

/**
 * @param {object} options
 * @param {() => string} options.getUrl Latest URL. Empty string means "cannot connect".
 * @param {boolean} [options.binary]
 * @param {(ws: WebSocket) => void} [options.prepare]
 * @param {(ws: WebSocket) => void} [options.onSocket] Fired before status updates so refs are current.
 * @param {(ws: WebSocket) => void} [options.onOpen]
 * @param {(event: MessageEvent, ws: WebSocket) => void} [options.onMessage]
 * @param {(event: CloseEvent) => void} [options.onClose]
 * @param {(status: string) => void} [options.onStatus]
 * @param {(event: CloseEvent) => void} [options.onAuthFailure] Stops retrying.
 * @param {number} [options.heartbeatMs] Send heartbeat() on this interval while open.
 * @param {number} [options.heartbeatTimeoutMs] One missed heartbeat. No inbound message for this long counts as one miss.
 * @param {number} [options.maxMissedHeartbeats] Reconnect after this many misses in a row. Defaults to 1.
 * @param {() => string | null} [options.heartbeat]
 * @param {number | (() => number)} [options.connectTimeoutMs] Abandon a socket that is still not open. A function is read on each attempt.
 * @param {boolean} [options.retryWhenUrlMissing]
 */
export function createSocketSession(options) {
  const initialMs = options.initialBackoffMs ?? DEFAULT_INITIAL_MS;
  const maxMs = options.maxBackoffMs ?? DEFAULT_MAX_MS;
  const factor = options.factor ?? DEFAULT_FACTOR;

  let ws = null;
  let generation = 0;
  let stopped = true;
  let attempt = 0;
  let retryTimer = null;
  let heartbeatTimer = null;
  let heartbeatWatchdog = null;
  let missedHeartbeats = 0;
  let connectTimer = null;
  let online = browserOnline();

  const setStatus = (status) => {
    try {
      options.onStatus?.(status);
    } catch {
      // Status listeners must not break the socket.
    }
  };

  const clearRetry = () => {
    if (retryTimer) {
      clearTimeout(retryTimer);
      retryTimer = null;
    }
  };

  const clearHeartbeat = () => {
    if (heartbeatTimer) clearInterval(heartbeatTimer);
    heartbeatTimer = null;
    clearWatchdog();
  };

  const clearWatchdog = () => {
    if (heartbeatWatchdog) {
      clearTimeout(heartbeatWatchdog);
      heartbeatWatchdog = null;
    }
  };

  const missLimit = () => {
    const limit = options.maxMissedHeartbeats;
    return limit > 0 ? limit : 1;
  };

  const clearConnectTimer = () => {
    if (connectTimer) {
      clearTimeout(connectTimer);
      connectTimer = null;
    }
  };

  // A powered-off device never completes the WebSocket close handshake, and
  // the browser can sit in CLOSING for about ten seconds. Drop our side of
  // the session immediately and let that close finish in the background.
  const dropConnection = (socket, myGen) => {
    if (myGen !== generation || stopped) return;
    clearConnectTimer();
    clearHeartbeat();
    if (socket) {
      socket.onopen = null;
      socket.onmessage = null;
      socket.onerror = null;
      socket.onclose = null;
      if (ws === socket) ws = null;
      try {
        socket.close(4000, "dropped");
      } catch {
        // ignore
      }
    }
    scheduleRetry();
  };

  // Arm once per unanswered heartbeat. Later sends must not extend the
  // window, or a silent server would never time out while we keep writing.
  // A window with no inbound message is one miss. The socket stays up until
  // maxMissedHeartbeats of those happen in a row, so a busy stream or upload
  // can delay a reply without forcing a reconnect.
  const armWatchdog = (socket, myGen) => {
    const timeoutMs = options.heartbeatTimeoutMs;
    if (!timeoutMs || heartbeatWatchdog) return;
    heartbeatWatchdog = setTimeout(() => {
      heartbeatWatchdog = null;
      if (myGen !== generation || stopped) return;
      missedHeartbeats += 1;
      if (missedHeartbeats >= missLimit()) {
        missedHeartbeats = 0;
        dropConnection(socket, myGen);
        return;
      }
      armWatchdog(socket, myGen);
    }, timeoutMs);
  };

  const startHeartbeat = (socket, myGen) => {
    clearHeartbeat();
    missedHeartbeats = 0;
    const interval = options.heartbeatMs;
    if (!interval || typeof options.heartbeat !== "function") return;

    const sendBeat = () => {
      if (myGen !== generation || stopped) return;
      if (socket.readyState !== WebSocket.OPEN) return;
      try {
        const payload = options.heartbeat();
        if (payload != null) socket.send(payload);
        armWatchdog(socket, myGen);
      } catch {
        dropConnection(socket, myGen);
      }
    };

    sendBeat();
    heartbeatTimer = setInterval(sendBeat, interval);
  };

  const scheduleRetry = () => {
    if (stopped) return;
    clearRetry();
    if (!online) {
      setStatus(SOCKET_STATUS.OFFLINE);
      return;
    }
    const base = Math.min(maxMs, Math.round(initialMs * Math.pow(factor, attempt)));
    attempt += 1;
    const delay = Math.round(base * (0.85 + Math.random() * 0.3));
    setStatus(SOCKET_STATUS.RECONNECTING);
    retryTimer = setTimeout(connect, delay);
  };

  function connect() {
    if (stopped) return;
    clearRetry();

    if (!online) {
      setStatus(SOCKET_STATUS.OFFLINE);
      return;
    }

    const url = options.getUrl?.() || "";
    if (!url) {
      setStatus(SOCKET_STATUS.ERROR);
      if (options.retryWhenUrlMissing) scheduleRetry();
      return;
    }

    const myGen = ++generation;

    const previous = ws;
    ws = null;
    if (previous) {
      previous.onopen = null;
      previous.onmessage = null;
      previous.onerror = null;
      previous.onclose = null;
      try {
        previous.close(1000, "reconnect");
      } catch {
        // ignore
      }
    }

    setStatus(attempt === 0 ? SOCKET_STATUS.CONNECTING : SOCKET_STATUS.RECONNECTING);

    let socket;
    try {
      socket = new WebSocket(url);
    } catch {
      scheduleRetry();
      return;
    }

    if (options.binary) socket.binaryType = "arraybuffer";
    try {
      options.prepare?.(socket);
    } catch {
      // ignore
    }

    ws = socket;

    const timeoutMs =
      typeof options.connectTimeoutMs === "function"
        ? options.connectTimeoutMs()
        : options.connectTimeoutMs;
    if (timeoutMs > 0) {
      connectTimer = setTimeout(() => {
        connectTimer = null;
        if (myGen !== generation || stopped) return;
        if (socket.readyState === WebSocket.OPEN) return;
        dropConnection(socket, myGen);
      }, timeoutMs);
    }

    socket.onopen = () => {
      if (myGen !== generation || stopped) return;
      clearConnectTimer();
      attempt = 0;
      try {
        options.onSocket?.(socket);
      } catch {
        // ignore
      }
      setStatus(SOCKET_STATUS.CONNECTED);
      startHeartbeat(socket, myGen);
      try {
        options.onOpen?.(socket);
      } catch {
        // ignore
      }
    };

    socket.onmessage = (event) => {
      if (myGen !== generation || stopped) return;
      // Any inbound message clears the miss streak. Silence is measured
      // from the last message, not the next heartbeat.
      missedHeartbeats = 0;
      clearWatchdog();
      armWatchdog(socket, myGen);
      try {
        options.onMessage?.(event, socket);
      } catch {
        // ignore
      }
    };

    socket.onerror = () => {
      // The close event carries the reason and schedules the retry.
    };

    socket.onclose = (event) => {
      if (myGen !== generation) return;
      clearConnectTimer();
      clearHeartbeat();
      if (ws === socket) ws = null;
      if (stopped) return;

      try {
        options.onClose?.(event);
      } catch {
        // ignore
      }

      if (isAuthClose(event)) {
        stopped = true;
        setStatus(SOCKET_STATUS.AUTH);
        try {
          options.onAuthFailure?.(event);
        } catch {
          // ignore
        }
        return;
      }

      scheduleRetry();
    };
  }

  const onOnline = () => {
    online = true;
    if (stopped) return;
    const dead =
      !ws ||
      ws.readyState === WebSocket.CLOSING ||
      ws.readyState === WebSocket.CLOSED;
    if (!dead) {
      if (ws.readyState === WebSocket.OPEN) setStatus(SOCKET_STATUS.CONNECTED);
      return;
    }
    attempt = 0;
    connect();
  };

  const onOffline = () => {
    online = false;
    clearRetry();
    if (!stopped) setStatus(SOCKET_STATUS.OFFLINE);
  };

  const onVisible = () => {
    if (typeof document === "undefined") return;
    if (document.visibilityState !== "visible" || stopped || !online) return;
    const dead =
      !ws ||
      ws.readyState === WebSocket.CLOSING ||
      ws.readyState === WebSocket.CLOSED;
    if (!dead) return;
    attempt = 0;
    connect();
  };

  if (typeof window !== "undefined") {
    window.addEventListener("online", onOnline);
    window.addEventListener("offline", onOffline);
    document.addEventListener("visibilitychange", onVisible);
  }

  return {
    start() {
      stopped = false;
      attempt = 0;
      online = browserOnline();
      connect();
    },
    stop() {
      stopped = true;
      generation += 1;
      clearRetry();
      clearConnectTimer();
      clearHeartbeat();
      const current = ws;
      ws = null;
      if (current) {
        current.onopen = null;
        current.onmessage = null;
        current.onerror = null;
        current.onclose = null;
        try {
          current.close(1000, "stop");
        } catch {
          // ignore
        }
      }
      setStatus(SOCKET_STATUS.DISCONNECTED);
    },
    getSocket() {
      return ws && ws.readyState === WebSocket.OPEN ? ws : null;
    },
    dispose() {
      this.stop();
      if (typeof window !== "undefined") {
        window.removeEventListener("online", onOnline);
        window.removeEventListener("offline", onOffline);
        document.removeEventListener("visibilitychange", onVisible);
      }
    },
  };
}
