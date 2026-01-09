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
  rssi: { localPath: "/ws/rssi", tunnelKey: "ws_rssi" },
  camera: { localPath: "/ws/camera", tunnelKey: "ws_camera" },
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

  if (preferTunnel && isTunnelReady(config)) {
    const baseUrl = config?.cloudTunnel?.baseUrl || "";
    const deviceId = config?.hostname || config?.deviceName || "";
    const token = config?.auth?.token || getAuthToken() || "";
    return buildTunnelWsUrl({
      baseUrl,
      deviceId,
      tunnelKey: def.tunnelKey,
      token,
    });
  }

  // LAN/local WS
  const mdnsHostname = config?.hostname || config?.deviceName || null;
  return buildWebSocketUrl(def.localPath, mdnsHostname);
}
