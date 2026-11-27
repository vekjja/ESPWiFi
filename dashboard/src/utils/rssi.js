/**
 * RSSI (Received Signal Strength Indicator) utility functions
 * Shared between button + settings/info UI components
 *
 * Signal quality thresholds:
 * - >= -60 dBm: Excellent (primary color)
 * - >= -80 dBm: Fair (warning color)
 * - < -80 dBm: Poor (error color)
 */

/**
 * Validate if RSSI value is a valid number
 * @param {number|null|undefined} rssi - The RSSI value to validate
 * @returns {boolean} True if valid number, false otherwise
 */
export const isValidRssi = (rssi) =>
  rssi !== null && rssi !== undefined && !Number.isNaN(Number(rssi));

/**
 * Get MUI theme color token based on RSSI signal strength
 * Suitable for use in sx prop for color/backgroundColor
 * @param {number|null|undefined} rssi - RSSI value in dBm
 * @returns {string} MUI theme color token (e.g., "primary.main", "warning.main")
 * @example
 * getRSSIThemeColor(-55) // Returns "primary.main" (excellent)
 * getRSSIThemeColor(-75) // Returns "warning.main" (fair)
 * getRSSIThemeColor(-85) // Returns "error.main" (poor)
 */
export const getRSSIThemeColor = (rssi) => {
  if (!isValidRssi(rssi)) return "text.disabled";
  const v = Number(rssi);
  if (v >= -60) return "primary.main";
  if (v >= -80) return "warning.main";
  return "error.main";
};

/**
 * Get MUI Chip color prop value based on RSSI signal strength
 * @param {number|null|undefined} rssi - RSSI value in dBm
 * @returns {string} MUI Chip color value ("primary", "warning", "error", or "default")
 * @example
 * getRSSIChipColor(-55) // Returns "primary"
 * getRSSIChipColor(null) // Returns "default"
 */
export const getRSSIChipColor = (rssi) => {
  if (!isValidRssi(rssi)) return "default";
  const v = Number(rssi);
  if (v >= -60) return "primary";
  if (v >= -80) return "warning";
  return "error";
};
