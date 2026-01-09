import { getAuthHeader, getAuthToken } from "./authUtils";

const normalizeProtocol = (protocol) => {
  if (!protocol) return "";
  // Accept "http", "http:", "https", "https:" (same for ws/wss)
  return protocol.endsWith(":") ? protocol : `${protocol}:`;
};

/**
 * Get the API base URL for HTTP requests
 * Uses environment variables or falls back to localhost:80
 * @returns {string} The API base URL
 */
export const getApiUrl = () => {
  // Allow explicit API override even in production (e.g. app on espwifi.io,
  // API on espwifi.local).
  const forcedHost = process.env.REACT_APP_API_HOST;
  if (forcedHost) {
    const forcedPort = process.env.REACT_APP_API_PORT || 80;
    const forcedProtocol = normalizeProtocol(
      process.env.REACT_APP_API_PROTOCOL ||
        // If we're explicitly pointing at a device host, default to HTTP
        // even when the dashboard is served over HTTPS (espwifi.io).
        "http:"
    );
    // If the page is HTTPS but the forced API protocol is HTTP, browsers will
    // block it as mixed content (especially on mobile). In that case, fall back
    // to same-origin URLs (which our App can then handle gracefully).
    if (window.location.protocol === "https:" && forcedProtocol === "http:") {
      return "";
    }
    return `${forcedProtocol}//${forcedHost}:${forcedPort}`;
  }

  // Default production behavior: use relative URLs (same-origin)
  if (process.env.NODE_ENV === "production") return "";

  // Use environment variables if set, otherwise default to localhost:80
  const hostname = process.env.REACT_APP_API_HOST || "localhost";
  const port = process.env.REACT_APP_API_PORT || 3000;
  return `http://${hostname}:${port}`;
};

/**
 * Get the WebSocket base URL
 * Converts HTTP protocol to WebSocket protocol
 * @param {string} mdnsHostname - Optional mDNS hostname
 * @returns {string} The WebSocket base URL
 */
export const getWebSocketUrl = (mdnsHostname = null) => {
  const hasForcedApiHost = Boolean(process.env.REACT_APP_API_HOST);
  let protocol = normalizeProtocol(
    process.env.REACT_APP_WS_PROTOCOL ||
      // If we're explicitly pointing at a device host, default to WS (not WSS)
      // even when the dashboard is served over HTTPS.
      (hasForcedApiHost
        ? "ws:"
        : window.location.protocol === "https:"
        ? "wss:"
        : "ws:")
  );
  // Browsers block ws:// from https:// pages. If we're on https, force wss://
  // to avoid Mixed Content / SecurityError spam.
  if (window.location.protocol === "https:" && protocol === "ws:") {
    protocol = "wss:";
  }

  // If an API host is explicitly forced, prefer it over any passed hostname.
  // This keeps WS behavior consistent with HTTP requests when the dashboard is
  // served from a different origin (e.g. espwifi.io).
  const forcedHost = process.env.REACT_APP_API_HOST;

  // Same rationale as getApiUrl(): on HTTPS pages, do not attempt ws:// to a
  // forced host (it will be blocked). Prefer same-origin wss:// instead.
  const canUseForcedHost =
    Boolean(forcedHost) &&
    !(
      window.location.protocol === "https:" &&
      normalizeProtocol(process.env.REACT_APP_WS_PROTOCOL || "ws:") === "ws:"
    );

  // Use provided hostname as-is (do NOT force ".local"; firmware may not run mDNS
  // and callers may already pass a full hostname like "espwifi.local").
  // Otherwise use environment variables, falling back to the current page host.
  const hostname =
    (canUseForcedHost ? forcedHost : null) || mdnsHostname || null;

  // If we're not forcing a host, use window.location.host so port matches the
  // current origin (important for https :443).
  if (!hostname) {
    return `${protocol}//${window.location.host}`;
  }

  const port = process.env.REACT_APP_API_PORT || 80;
  return `${protocol}//${hostname}:${port}`;
};

/**
 * Build a full API URL
 * @param {string} path - The API endpoint path (e.g., "/config", "/camera/snapshot")
 * @param {string} mdnsHostname - Optional mDNS hostname to use instead of default hostname
 * @returns {string} The full API URL
 */
export const buildApiUrl = (path, mdnsHostname = null) => {
  // If an API host is explicitly forced, always use it (even if a caller passes
  // an mDNS hostname). This keeps behavior consistent across the app.
  if (process.env.REACT_APP_API_HOST) {
    return `${getApiUrl()}${path}`;
  }

  // If mDNS hostname is provided, use it with port 80
  if (mdnsHostname) {
    const protocol = normalizeProtocol(
      process.env.REACT_APP_API_PROTOCOL || "http:"
    );
    return `${protocol}//${mdnsHostname}:80${path}`;
  }

  // Always use getApiUrl() - it handles both development and production correctly
  return `${getApiUrl()}${path}`;
};

/**
 * Build a full WebSocket URL
 * @param {string} path - The WebSocket endpoint path (e.g., "/ws/camera", "/ws/rssi")
 * @param {string} mdnsHostname - Optional mDNS hostname
 * @returns {string} The full WebSocket URL
 */
export const buildWebSocketUrl = (path, mdnsHostname = null) => {
  const wsUrl = getWebSocketUrl(mdnsHostname);
  let url = `${wsUrl}${path}`;

  // WebSocket API doesn't allow setting custom headers.
  // Pass auth token via query param so firmware can validate connections.
  const token = getAuthToken();

  // Only add token parameter if we have a valid token
  if (
    token &&
    token !== "null" &&
    token !== "undefined" &&
    token.trim() !== ""
  ) {
    const sep = url.includes("?") ? "&" : "?";
    url = `${url}${sep}token=${encodeURIComponent(token)}`;
  }

  return url;
};

/**
 * Get default fetch options with authentication headers
 * @param {Object} options - Additional fetch options to merge
 * @returns {Object} Fetch options with auth headers
 */
export const getFetchOptions = (options = {}) => {
  const authHeader = getAuthHeader();
  const headers = { ...options.headers };

  // Only set Content-Type if not already set and body is not FormData
  if (!headers["Content-Type"] && !(options.body instanceof FormData)) {
    headers["Content-Type"] = "application/json";
  }

  if (authHeader) {
    headers["Authorization"] = authHeader;
  }

  return {
    ...options,
    headers,
  };
};
