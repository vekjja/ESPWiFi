import { getAuthHeader } from "./authUtils";

/**
 * Get the API base URL for HTTP requests
 * Uses environment variables or falls back to localhost:80
 * @returns {string} The API base URL
 */
export const getApiUrl = () => {
  // In production, use relative URLs
  if (process.env.NODE_ENV === "production") {
    return "";
  }

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
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";

  // Use mDNS hostname if provided, otherwise use environment variables or localhost
  const hostname = mdnsHostname
    ? `${mdnsHostname}.local`
    : process.env.REACT_APP_API_HOST || "localhost";
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
  // If mDNS hostname is provided, use it with port 80
  if (mdnsHostname) {
    const protocol = window.location.protocol === "https:" ? "https:" : "http:";
    return `${protocol}//${mdnsHostname}.local:80${path}`;
  }

  // Always use getApiUrl() - it handles both development and production correctly
  return `${getApiUrl()}${path}`;
};

/**
 * Build a full WebSocket URL
 * @param {string} path - The WebSocket endpoint path (e.g., "/camera", "/rssi")
 * @param {string} mdnsHostname - Optional mDNS hostname
 * @returns {string} The full WebSocket URL
 */
export const buildWebSocketUrl = (path, mdnsHostname = null) => {
  const wsUrl = getWebSocketUrl(mdnsHostname);
  return `${wsUrl}${path}`;
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
