/**
 * Authentication utilities for managing tokens and auth state
 */

const TOKEN_KEY = "espwifi_auth_token";

/**
 * Get the stored authentication token
 * @returns {string|null} The authentication token or null if not found
 */
export const getAuthToken = () => {
  return localStorage.getItem(TOKEN_KEY);
};

/**
 * Store the authentication token
 * @param {string} token - The authentication token to store
 */
export const setAuthToken = (token) => {
  if (token) {
    localStorage.setItem(TOKEN_KEY, token);
  } else {
    localStorage.removeItem(TOKEN_KEY);
  }
};

/**
 * Check if user is authenticated
 * @returns {boolean} True if token exists
 */
export const isAuthenticated = () => {
  return !!getAuthToken();
};

/**
 * Clear authentication token (logout)
 */
export const clearAuthToken = () => {
  localStorage.removeItem(TOKEN_KEY);
};

/**
 * Get Authorization header value
 * @returns {string|null} Bearer token string or null
 */
export const getAuthHeader = () => {
  const token = getAuthToken();
  return token ? `Bearer ${token}` : null;
};
