/**
 * Get the API base URL for HTTP requests
 * Uses environment variables or falls back to localhost
 * @returns {string} The API base URL
 */
export const getApiUrl = () => {
  const hostname = process.env.REACT_APP_API_HOST || "localhost";
  const port = process.env.REACT_APP_API_PORT || 80;
  return process.env.NODE_ENV === "production"
    ? ""
    : `http://${hostname}:${port}`;
};

/**
 * Get the WebSocket base URL
 * Converts HTTP protocol to WebSocket protocol
 * @param {string} mdnsHostname - Optional mDNS hostname
 * @returns {string} The WebSocket base URL
 */
export const getWebSocketUrl = (mdnsHostname = null) => {
  const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";

  // Use mDNS hostname if provided, otherwise use current hostname
  const hostname = mdnsHostname
    ? `${mdnsHostname}.local`
    : window.location.hostname;

  // Include port only if we're not using mDNS
  const port =
    window.location.port && !mdnsHostname ? `:${window.location.port}` : "";

  return `${protocol}//${hostname}${port}`;
};

/**
 * Build a full API URL
 * @param {string} path - The API endpoint path (e.g., "/config", "/camera/snapshot")
 * @param {string} mdnsHostname - Optional mDNS hostname to use instead of default hostname
 * @returns {string} The full API URL
 */
export const buildApiUrl = (path, mdnsHostname = null) => {
  let apiUrl = getApiUrl();

  // If mDNS hostname is provided and we're not in production, use it
  if (mdnsHostname && process.env.NODE_ENV !== "production") {
    const protocol = window.location.protocol === "https:" ? "https:" : "http:";
    apiUrl = `${protocol}//${mdnsHostname}.local`;
  }

  return `${apiUrl}${path}`;
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
