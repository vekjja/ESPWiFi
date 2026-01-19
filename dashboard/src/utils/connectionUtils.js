import { buildWebSocketUrl } from "./apiUtils";
import { getAuthToken } from "./authUtils";

// One source of truth for how the dashboard connects in 3 hosting contexts:
// - localhost:3000 (dev)
// - device-hosted (espwifi.local / espwifi-XXXX / device IP)
// - cloud-hosted (espwifi.io)
//
// Rule: if a device is paired (cloudTunnel.enabled + baseUrl + token + deviceId),
// we prefer tunnel WS for camera/rssi/control. Otherwise use LAN WS.

const ENDPOINTS = {
  // RSSI + camera are both carried over the control socket now.
  rssi: { localPath: "/ws/control", tunnelKey: "ws_control" },
  camera: { localPath: "/ws/control", tunnelKey: "ws_control" },
  control: { localPath: "/ws/control", tunnelKey: "ws_control" },
};

function normalizeProtocol(p) {
  if (!p) return "";
  return p.endsWith(":") ? p : `${p}:`;
}

function isPrivateIp(hostname) {
  // Basic check for typical LAN ranges; good enough for routing logic.
  const m = /^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/.exec(hostname || "");
  if (!m) return false;
  const a = Number(m[1]);
  const b = Number(m[2]);
  if (a === 10) return true;
  if (a === 192 && b === 168) return true;
  if (a === 172 && b >= 16 && b <= 31) return true;
  return false;
}

export function getHostContext() {
  const hostname = window.location.hostname;
  const protocol = normalizeProtocol(window.location.protocol);
  const isHttps = protocol === "https:";
  const isLocalhost =
    hostname === "localhost" || hostname === "127.0.0.1" || hostname === "::1";
  const isCloudHost =
    hostname === "espwifi.io" || hostname.endsWith(".espwifi.io");
  const isDeviceLikeHost =
    !isCloudHost &&
    !isLocalhost &&
    (hostname.endsWith(".local") ||
      isPrivateIp(hostname) ||
      hostname.includes("espwifi"));

  return {
    hostname,
    protocol,
    isHttps,
    isLocalhost,
    isCloudHost,
    isDeviceLikeHost,
  };
}

export function isTunnelReady(config) {
  const baseUrl = config?.cloudTunnel?.baseUrl || "";
  const enabled = Boolean(config?.cloudTunnel?.enabled);
  const deviceId = config?.hostname || config?.deviceName || "";
  const token = config?.auth?.token || getAuthToken() || "";
  return Boolean(enabled && baseUrl && deviceId && token);
}

export function buildTunnelWsUrl({ baseUrl, deviceId, tunnelKey, token }) {
  const base = String(baseUrl || "").replace(/\/+$/, "");
  if (!base || !deviceId || !tunnelKey) return "";
  const tok = token || "";

  // Accept https://, http://, wss://, ws://
  let wsBase = base;
  if (base.startsWith("https://"))
    wsBase = `wss://${base.slice("https://".length)}`;
  if (base.startsWith("http://"))
    wsBase = `ws://${base.slice("http://".length)}`;

  // If base has no scheme, assume it's already ws(s) host or an https host string.
  if (!wsBase.startsWith("ws://") && !wsBase.startsWith("wss://")) {
    // default to wss in cloud contexts, ws otherwise
    const { isHttps } = getHostContext();
    wsBase = `${isHttps ? "wss://" : "ws://"}${wsBase.replace(
      /^https?:\/\//,
      ""
    )}`;
  }

  let url = `${wsBase}/ws/ui/${encodeURIComponent(
    deviceId
  )}?tunnel=${encodeURIComponent(tunnelKey)}`;
  if (tok) {
    url += `&token=${encodeURIComponent(tok)}`;
  }
  return url;
}

export function resolveWebSocketUrl(endpoint, config, opts = {}) {
  const def = ENDPOINTS[endpoint];
  if (!def) return "";

  const preferTunnel = opts.preferTunnel !== false; // default true

  // If the UI is served from the device itself, always use same-origin WebSockets.
  // This avoids breakage when the device's mDNS hostname changes (e.g. espwifi.local -> spark.local)
  // and avoids relying on any stored/baked hostname values.
  const hostCtx = getHostContext();
  if (
    hostCtx.isDeviceLikeHost &&
    !hostCtx.isCloudHost &&
    !hostCtx.isLocalhost
  ) {
    return buildWebSocketUrl(def.localPath);
  }

  // If device provided a cloud tunnel URL, use it
  if (preferTunnel && config?.cloudTunnel?.wsUrl) {
    let url = config.cloudTunnel.wsUrl;

    // Ensure auth token is appended if available
    // Check both flat (config.authToken) and nested (config.auth.token) locations
    const token =
      config?.authToken || config?.auth?.token || getAuthToken() || "";
    console.log("[connectionUtils] Cloud URL token check:", {
      hasConfigAuthToken: !!config?.authToken,
      hasNestedAuthToken: !!config?.auth?.token,
      hasStorageToken: !!getAuthToken(),
      finalToken: token ? `${token.substring(0, 4)}...` : "none",
      urlHasToken: url.includes("token="),
    });

    if (token && !url.includes("token=")) {
      const separator = url.includes("?") ? "&" : "?";
      url = `${url}${separator}token=${encodeURIComponent(token)}`;
      console.log("[connectionUtils] Appended token to cloud URL:", url);
    }

    return url;
  }

  // Otherwise, use local/LAN WebSocket
  const mdnsHostname = config?.hostname || config?.deviceName || null;
  return buildWebSocketUrl(def.localPath, mdnsHostname);
}
