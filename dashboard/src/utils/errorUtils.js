/**
 * Centralized error handling utilities
 * Provides consistent error messages and handling across the application
 */

/**
 * Error types for categorizing different failure scenarios
 */
export const ErrorType = {
  NETWORK: "NETWORK",
  TIMEOUT: "TIMEOUT",
  AUTH: "AUTH",
  VALIDATION: "VALIDATION",
  SERVER: "SERVER",
  UNKNOWN: "UNKNOWN",
};

/**
 * Determine error type from error object
 * @param {Error} error - Error object to categorize
 * @returns {string} Error type from ErrorType enum
 */
export const getErrorType = (error) => {
  if (!error) return ErrorType.UNKNOWN;

  if (error.name === "AbortError") return ErrorType.TIMEOUT;
  if (error.message?.includes("Failed to fetch")) return ErrorType.NETWORK;
  if (error.message?.includes("Unauthorized") || error.message?.includes("401"))
    return ErrorType.AUTH;
  if (error.message?.includes("400") || error.message?.includes("Invalid"))
    return ErrorType.VALIDATION;
  if (
    error.message?.includes("500") ||
    error.message?.includes("502") ||
    error.message?.includes("503")
  )
    return ErrorType.SERVER;

  return ErrorType.UNKNOWN;
};

/**
 * Get user-friendly error message based on error type
 * @param {Error} error - Error object
 * @param {string} context - Context description (e.g., "fetching configuration")
 * @returns {string} User-friendly error message
 */
export const getUserFriendlyErrorMessage = (error, context = "operation") => {
  const errorType = getErrorType(error);

  switch (errorType) {
    case ErrorType.TIMEOUT:
      return `Request timed out while ${context}. Device may be busy or offline.`;
    case ErrorType.NETWORK:
      return `Network error while ${context}. Please check your connection.`;
    case ErrorType.AUTH:
      return `Authentication required. Please log in again.`;
    case ErrorType.VALIDATION:
      return `Invalid data provided for ${context}. Please check your input.`;
    case ErrorType.SERVER:
      return `Server error while ${context}. Please try again later.`;
    default:
      return `Error while ${context}: ${error.message || "Unknown error"}`;
  }
};

/**
 * Determine if error should be retried automatically
 * @param {Error} error - Error object
 * @returns {boolean} True if error is retryable
 */
export const isRetryableError = (error) => {
  const errorType = getErrorType(error);
  return (
    errorType === ErrorType.NETWORK ||
    errorType === ErrorType.TIMEOUT ||
    errorType === ErrorType.SERVER
  );
};

/**
 * Log error to console with consistent formatting
 * Only logs in development or for important errors
 * @param {Error} error - Error object
 * @param {string} context - Context description
 * @param {boolean} forceLog - Force logging even in production
 */
export const logError = (error, context = "", forceLog = false) => {
  if (process.env.NODE_ENV === "production" && !forceLog) return;

  const errorType = getErrorType(error);
  const prefix = context ? `[${context}]` : "";

  console.error(
    `${prefix} ${errorType}:`,
    error.message || error,
    error.stack ? `\n${error.stack}` : ""
  );
};

/**
 * Handle API fetch errors with consistent error handling
 * @param {Response} response - Fetch response object
 * @param {string} context - Context description
 * @throws {Error} Throws error with user-friendly message
 */
export const handleFetchResponse = async (response, context = "API call") => {
  if (!response.ok) {
    let errorMessage = `HTTP ${response.status}: ${response.statusText}`;

    // Try to extract error details from response body
    try {
      const errorData = await response.json();
      if (errorData.message || errorData.error) {
        errorMessage = errorData.message || errorData.error;
      }
    } catch {
      // Response body is not JSON, use status text
    }

    const error = new Error(errorMessage);
    error.status = response.status;
    throw error;
  }

  return response;
};

