/**
 * Convert bytes to human-readable format
 * @param {number} bytes - Number of bytes to convert
 * @returns {string} Human-readable string (e.g., "1.5 MB", "512 KB")
 */
export const bytesToHumanReadable = (bytes) => {
  if (!bytes || bytes === 0) return "0 B";

  const units = ["B", "KB", "MB", "GB"];
  let unitIndex = 0;
  let size = bytes;

  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024;
    unitIndex++;
  }

  if (unitIndex === 0) {
    return `${Math.round(size)} ${units[unitIndex]}`;
  } else {
    return `${size.toFixed(1)} ${units[unitIndex]}`;
  }
};

/**
 * Format uptime in seconds to human-readable format
 * @param {number} uptimeSeconds - Uptime in seconds
 * @returns {string} Formatted uptime (e.g., "2h 30m", "45m", "1d 5h")
 */
export const formatUptime = (uptimeSeconds) => {
  if (!uptimeSeconds) return "N/A";

  const days = Math.floor(uptimeSeconds / 86400);
  const hours = Math.floor((uptimeSeconds % 86400) / 3600);
  const minutes = Math.floor((uptimeSeconds % 3600) / 60);

  const parts = [];
  if (days > 0) parts.push(`${days}d`);
  if (hours > 0) parts.push(`${hours}h`);
  if (minutes > 0) parts.push(`${minutes}m`);

  return parts.length > 0 ? parts.join(" ") : "0m";
};
