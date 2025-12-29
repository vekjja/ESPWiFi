/**
 * RSSI helpers (shared between button + settings/info UI)
 *
 * Thresholds match the RSSI button behavior:
 * - >= -60 dBm: good (primary)
 * - >= -80 dBm: fair (warning)
 * - else: poor (error)
 */

export const isValidRssi = (rssi) =>
  rssi !== null && rssi !== undefined && !Number.isNaN(Number(rssi));

/**
 * Returns a theme color token suitable for sx color/backgroundColor.
 * @param {number|null|undefined} rssi
 */
export const getRSSIThemeColor = (rssi) => {
  if (!isValidRssi(rssi)) return "text.disabled";
  const v = Number(rssi);
  if (v >= -60) return "primary.main";
  if (v >= -80) return "warning.main";
  return "error.main";
};

/**
 * Returns an MUI Chip color prop value.
 * @param {number|null|undefined} rssi
 */
export const getRSSIChipColor = (rssi) => {
  if (!isValidRssi(rssi)) return "default";
  const v = Number(rssi);
  if (v >= -60) return "primary";
  if (v >= -80) return "warning";
  return "error";
};
